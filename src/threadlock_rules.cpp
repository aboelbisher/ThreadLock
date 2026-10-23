#include "threadlock_rules.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <utility>

namespace threadlock {
namespace {

constexpr int kMaxTicks = 20000;  // a run that hasn't ended by then counts as jammed (a safety net)

struct ColorInfo {
  Color color;
  const char* name;
  char letter;
};
constexpr ColorInfo kColors[] = {
    {Color::None, "none", '-'},   {Color::Blue, "blue", 'B'}, {Color::Amber, "amber", 'A'},
    {Color::Green, "green", 'G'}, {Color::Rose, "rose", 'R'}, {Color::Pearl, "pearl", 'P'},
    {Color::Ink, "ink", 'K'},     {Color::Mud, "mud", 'M'},
};

struct PieceInfoRow {
  PieceType type;
  const char* name;
  const char* key;
  bool setting;
};
constexpr PieceInfoRow kPieceRows[] = {
    {PieceType::Separator, "Separator", "separator", true}, {PieceType::Turnstile, "Turnstile", "turnstile", true},
    {PieceType::TankShort, "Short tank", "tank4", false},   {PieceType::TankTall, "Tall tank", "tank8", false},
    {PieceType::Flipper, "Flipper", "flipper", true},       {PieceType::Overflow, "Overflow", "overflow", true},
    {PieceType::Bridge, "Bridge", "bridge", false},         {PieceType::Mixer, "Mixer", "mixer", false},
    {PieceType::TwinGate, "Twin gate", "twin", false},
};

constexpr const char* kNodeKinds[] = {"spout", "fork",  "pass", "cross", "merge",
                                      "cup",   "drain", "rack", "mill",  "gate"};
constexpr const char* kLevelTypes[] = {"build", "repair", "spectacle", "challenge", "lab"};
constexpr const char* kFailures[] = {"none", "mud", "wrong_cup", "spill", "dry", "jam"};
constexpr const char* kRoles[] = {"acts", "idle", "trap"};

float Distance(Vec2 a, Vec2 b) { return std::hypot(b.x - a.x, b.y - a.y); }

Vec2 Lerp(Vec2 a, Vec2 b, float t) { return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t}; }

// The same on every device (see Level::Finalize): doubles and plain sqrt only.
double ExactLength(const std::vector<Vec2>& points) {
  double length = 0.0;
  for (size_t i = 1; i < points.size(); ++i) {
    const double dx = static_cast<double>(points[i].x) - static_cast<double>(points[i - 1].x);
    const double dy = static_cast<double>(points[i].y) - static_cast<double>(points[i - 1].y);
    length += std::sqrt(dx * dx + dy * dy);
  }
  return length;
}

std::array<double, 2> ExactDirection(Vec2 from, Vec2 to) {
  const double dx = static_cast<double>(to.x) - static_cast<double>(from.x);
  const double dy = static_cast<double>(to.y) - static_cast<double>(from.y);
  const double d = std::sqrt(dx * dx + dy * dy);
  return d > 0.0 ? std::array<double, 2>{dx / d, dy / d} : std::array<double, 2>{0.0, 0.0};
}

// Cuts `trim` units off the start of a polyline.
std::vector<Vec2> TrimStart(std::vector<Vec2> points, float trim) {
  while (points.size() >= 2 && trim > 0.0f) {
    const float segment = Distance(points[0], points[1]);
    if (segment > trim) {
      points[0] = Lerp(points[0], points[1], trim / segment);
      break;
    }
    trim -= segment;
    points.erase(points.begin());
  }
  return points;
}

std::vector<Vec2> TrimEnd(std::vector<Vec2> points, float trim) {
  std::reverse(points.begin(), points.end());
  points = TrimStart(std::move(points), trim);
  std::reverse(points.begin(), points.end());
  return points;
}

float PolylineLength(const std::vector<Vec2>& points) {
  float length = 0.0f;
  for (size_t i = 1; i < points.size(); ++i) length += Distance(points[i - 1], points[i]);
  return length;
}

}  // namespace

// ---- Liquids ----

Bead Bead::Blend(Color x, Color y) {
  if (static_cast<int>(y) < static_cast<int>(x)) std::swap(x, y);
  return {x, y};
}

Color Mix(Color x, Color y) {
  if (x == Color::None || y == Color::None) return x == Color::None ? y : x;
  if (x == y) return x;
  if (static_cast<int>(y) < static_cast<int>(x)) std::swap(x, y);
  if (x == Color::Blue && y == Color::Amber) return Color::Green;
  if (x == Color::Green && y == Color::Rose) return Color::Pearl;
  return Color::Mud;
}

const char* ColorName(Color color) { return kColors[static_cast<size_t>(color)].name; }

char ColorLetter(Color color) { return kColors[static_cast<size_t>(color)].letter; }

bool ParseColor(std::string_view text, Color& out) {
  for (const ColorInfo& info : kColors) {
    if (text == info.name || (text.size() == 1 && text[0] == info.letter)) {
      out = info.color;
      return out != Color::None;
    }
  }
  return false;
}

std::string BeadText(const Bead& bead) {
  std::string text(1, ColorLetter(bead.a));
  if (bead.IsBlend()) {
    text += '+';
    text += ColorLetter(bead.b);
  }
  return text;
}

bool ParseBead(std::string_view text, Bead& out) {
  const size_t plus = text.find('+');
  Color a = Color::None, b = Color::None;
  if (plus == std::string_view::npos) {
    if (!ParseColor(text, a)) return false;
    out = Bead::Pure(a);
    return true;
  }
  if (!ParseColor(text.substr(0, plus), a) || !ParseColor(text.substr(plus + 1), b)) return false;
  out = Bead::Blend(a, b);
  return true;
}

bool ParseBeads(std::string_view text, std::vector<Bead>& out) {
  out.clear();
  size_t i = 0;
  while (i < text.size()) {
    while (i < text.size() && text[i] == ' ') ++i;
    if (i >= text.size()) break;
    size_t end = text.find(' ', i);
    if (end == std::string_view::npos) end = text.size();
    std::string_view token = text.substr(i, end - i);
    i = end;
    int repeat = 1;
    const size_t star = token.find('*');
    if (star != std::string_view::npos) {
      repeat = 0;
      for (char c : token.substr(star + 1)) {
        if (c < '0' || c > '9') return false;
        repeat = repeat * 10 + (c - '0');
      }
      token = token.substr(0, star);
      if (repeat < 1 || repeat > 999) return false;
    }
    Bead bead;
    if (!ParseBead(token, bead)) return false;
    out.insert(out.end(), static_cast<size_t>(repeat), bead);
  }
  return !out.empty();
}

std::string BeadsText(const std::vector<Bead>& beads) {
  std::string text;
  for (size_t i = 0; i < beads.size();) {
    size_t j = i;
    while (j < beads.size() && beads[j] == beads[i]) ++j;
    if (!text.empty()) text += ' ';
    text += BeadText(beads[i]);
    if (j - i > 1) text += '*' + std::to_string(j - i);
    i = j;
  }
  return text;
}

// ---- Pieces ----

const char* PieceName(PieceType type) { return kPieceRows[static_cast<size_t>(type)].name; }

const char* PieceKey(PieceType type) { return kPieceRows[static_cast<size_t>(type)].key; }

bool ParsePieceType(std::string_view text, PieceType& out) {
  for (const PieceInfoRow& row : kPieceRows) {
    if (text == row.key) {
      out = row.type;
      return true;
    }
  }
  return false;
}

bool HasSetting(PieceType type) { return kPieceRows[static_cast<size_t>(type)].setting; }

int TankExtra(PieceType type) { return type == PieceType::TankShort ? 4 : type == PieceType::TankTall ? 8 : 0; }

const char* NodeKindKey(NodeKind kind) { return kNodeKinds[static_cast<size_t>(kind)]; }

bool ParseNodeKind(std::string_view text, NodeKind& out) {
  for (size_t i = 0; i < std::size(kNodeKinds); ++i) {
    if (text == kNodeKinds[i]) {
      out = static_cast<NodeKind>(i);
      return true;
    }
  }
  return false;
}

bool IsSocketKind(NodeKind kind) {
  return kind == NodeKind::Fork || kind == NodeKind::Pass || kind == NodeKind::Cross || kind == NodeKind::Merge;
}

// DESIGN.md §3, "Every piece in every socket".
PieceRole Role(PieceType type, NodeKind kind, bool twin_pair) {
  using enum PieceType;
  switch (kind) {
    case NodeKind::Fork:  // 1 in, 2 out: routers and tanks work (the flap sends a tank's drops on)
      if (type == Turnstile || type == Bridge) return PieceRole::Idle;
      if (type == Mixer || type == TwinGate) return PieceRole::Trap;
      return PieceRole::Acts;
    case NodeKind::Pass:  // 1 in, 1 out: only time works here, and a twin gate on a twin pair
      if (type == TankShort || type == TankTall) return PieceRole::Acts;
      if (type == TwinGate) return twin_pair ? PieceRole::Acts : PieceRole::Trap;
      if (type == Mixer) return PieceRole::Trap;
      return PieceRole::Idle;
    case NodeKind::Cross: return PieceRole::Acts;  // two lanes: every piece has a job
    case NodeKind::Merge:                          // 2 in, 1 out: nothing to route, no lanes to lift apart
      if (type == Separator || type == Flipper || type == Overflow || type == Bridge) return PieceRole::Idle;
      return PieceRole::Acts;
    default: return PieceRole::Idle;
  }
}

const char* PieceRoleKey(PieceRole role) { return kRoles[static_cast<size_t>(role)]; }

const char* LevelTypeKey(LevelType type) { return kLevelTypes[static_cast<size_t>(type)]; }

bool ParseLevelType(std::string_view text, LevelType& out) {
  for (size_t i = 0; i < std::size(kLevelTypes); ++i) {
    if (text == kLevelTypes[i]) {
      out = static_cast<LevelType>(i);
      return true;
    }
  }
  return false;
}

const char* FailureKey(Failure failure) { return kFailures[static_cast<size_t>(failure)]; }

// ---- Level ----

int Level::NodeIndex(std::string_view node_id) const {
  for (size_t i = 0; i < nodes.size(); ++i) {
    if (nodes[i].id == node_id) return static_cast<int>(i);
  }
  return -1;
}

int Level::CupCount() const {
  int cups = 0;
  for (const Node& node : nodes) {
    if (node.kind == NodeKind::Cup) ++cups;
    if (node.kind == NodeKind::Rack) cups += static_cast<int>(node.slots.size());
  }
  return cups;
}

int Level::Drops() const {
  int drops = 0;
  for (const Node& node : nodes) {
    for (const Bead& bead : node.beads) drops += bead.Drops();
  }
  return drops;
}

std::vector<std::string> Level::Finalize() {
  std::vector<std::string> problems;
  auto problem = [&](const std::string& text) { problems.push_back(id + ": " + text); };

  for (Node& node : nodes) {
    node.ins.clear();
    node.outs.clear();
    switch (node.kind) {
      case NodeKind::Spout: node.dwell = 0, node.radius = 22.0f; break;
      case NodeKind::Fork: node.dwell = 1, node.radius = 5.0f; break;
      case NodeKind::Pass: node.dwell = 1, node.radius = 5.0f; break;
      case NodeKind::Cross:
      case NodeKind::Merge:
      case NodeKind::Mill: node.dwell = kJunctionDwell, node.radius = 15.0f; break;
      case NodeKind::Cup:
      case NodeKind::Rack: node.dwell = 0, node.radius = 0.0f; break;
      case NodeKind::Drain: node.dwell = 0, node.radius = 14.0f; break;
      case NodeKind::Gate: node.dwell = 1, node.radius = 5.0f; break;
    }
  }
  for (size_t i = 0; i < nodes.size(); ++i) {
    for (size_t j = i + 1; j < nodes.size(); ++j) {
      if (nodes[i].id == nodes[j].id) problem("two nodes are called '" + nodes[i].id + "'");
    }
  }
  for (size_t p = 0; p < pipes.size(); ++p) {
    Pipe& pipe = pipes[p];
    if (pipe.from < 0 || pipe.to < 0 || static_cast<size_t>(pipe.from) >= nodes.size() ||
        static_cast<size_t>(pipe.to) >= nodes.size()) {
      problem("pipe " + std::to_string(p) + " joins a node that doesn't exist");
      return problems;
    }
    nodes[static_cast<size_t>(pipe.from)].outs.push_back(static_cast<int>(p));
    nodes[static_cast<size_t>(pipe.to)].ins.push_back(static_cast<int>(p));
  }

  for (size_t n = 0; n < nodes.size(); ++n) {
    Node& node = nodes[n];
    const size_t ins = node.ins.size(), outs = node.outs.size();
    auto expect = [&](size_t want_ins, size_t want_outs) {
      if (ins != want_ins || outs != want_outs) {
        problem("node '" + node.id + "' (" + NodeKindKey(node.kind) + ") has " + std::to_string(ins) + " in / " +
                std::to_string(outs) + " out, wants " + std::to_string(want_ins) + " / " + std::to_string(want_outs));
      }
    };
    switch (node.kind) {
      case NodeKind::Spout:
        expect(0, 1);
        if (node.beads.empty()) problem("spout '" + node.id + "' pours nothing");
        if (node.beat < 1 || node.start < 1) problem("spout '" + node.id + "' needs beat >= 1 and start >= 1");
        break;
      case NodeKind::Fork:
        expect(1, 2);
        if (node.flap != 0 && node.flap != 1) problem("fork '" + node.id + "' has a flap other than 0 / 1");
        break;
      case NodeKind::Pass: expect(1, 1); break;
      case NodeKind::Cross: expect(2, 2); break;
      case NodeKind::Merge: expect(2, 1); break;
      case NodeKind::Cup:
        expect(1, 0);
        if (node.count < 1 || node.want == Color::None) problem("cup '" + node.id + "' wants nothing");
        break;
      case NodeKind::Drain: expect(1, 0); break;
      case NodeKind::Rack:
        expect(1, 0);
        if (node.slots.empty()) problem("rack '" + node.id + "' has no bottles");
        for (const Slot& slot : node.slots) {
          if (slot.count < 1 || slot.want == Color::None) problem("a bottle of rack '" + node.id + "' wants nothing");
        }
        if (node.period < 1) problem("rack '" + node.id + "' needs period >= 1");
        if (node.first < 0 || static_cast<size_t>(node.first) >= std::max<size_t>(1, node.slots.size()))
          problem("rack '" + node.id + "' starts at a bottle it doesn't have");
        break;
      case NodeKind::Mill:
        expect(1, 1);
        if (node.gears.empty()) problem("mill '" + node.id + "' turns no gates");
        for (int gear : node.gears) {
          if (gear < 0 || static_cast<size_t>(gear) >= nodes.size() ||
              nodes[static_cast<size_t>(gear)].kind != NodeKind::Gate) {
            problem("mill '" + node.id + "' is geared to something that isn't a gate");
          }
        }
        break;
      case NodeKind::Gate: expect(1, 1); break;
    }
    if (node.socket && !IsSocketKind(node.kind)) {
      problem("node '" + node.id + "' (" + NodeKindKey(node.kind) + ") can't be a socket");
    }
    if (!node.gears.empty() && node.kind != NodeKind::Mill)
      problem("node '" + node.id + "' has gears but isn't a mill");
    if (node.rods.size() > node.gears.size()) problem("mill '" + node.id + "' has more rods than gears");
    if (node.twin >= 0) {
      if (static_cast<size_t>(node.twin) >= nodes.size() || node.kind != NodeKind::Pass ||
          nodes[static_cast<size_t>(node.twin)].kind != NodeKind::Pass ||
          nodes[static_cast<size_t>(node.twin)].twin != static_cast<int>(n)) {
        problem("node '" + node.id + "' has a broken twin");
      }
    }
  }
  if (!problems.empty()) return problems;

  // Pipes: the drawn line runs from the edge of one node to the edge of the next; its cells come
  // from that length (one speed everywhere). The cells must come out the same on every device (the
  // pars are baked on a PC, the Lab's puzzles are generated on each phone), so they're measured in
  // doubles with plain sqrt, which IEEE 754 rounds exactly everywhere (std::hypot and float sums
  // differ between math libraries, and a length near half a cell would round differently).
  for (Pipe& pipe : pipes) {
    const Node& from = nodes[static_cast<size_t>(pipe.from)];
    const Node& to = nodes[static_cast<size_t>(pipe.to)];
    std::vector<Vec2> line;
    line.push_back(from.at);
    line.insert(line.end(), pipe.via.begin(), pipe.via.end());
    line.push_back(to.at);
    const int segments = static_cast<int>(line.size()) - 1;
    // A coil winds its segment round the segment's middle: the drops' way gets longer.
    int coil_points = 0;
    pipe.coil_a = pipe.coil_b = {};
    if (pipe.coil_segment >= 0) {
      const int k = pipe.coil_segment;
      if (k >= segments || pipe.coil_turns < 1 || pipe.coil_turns > 8) {
        problem("the coil on " + from.id + " -> " + to.id + " needs a segment of the pipe and 1-8 turns");
      } else {
        const Vec2 a = line[static_cast<size_t>(k)], b = line[static_cast<size_t>(k) + 1];
        const double length = ExactLength({a, b});
        const double span = static_cast<double>(kCoilPitch) * pipe.coil_turns;
        if (length < span + 16.0) {
          problem("the coil on " + from.id + " -> " + to.id + " doesn't fit its segment");
        } else {
          const std::array<double, 2> u = ExactDirection(a, b);
          const double lead = (length - span) * 0.5;
          pipe.coil_a = {static_cast<float>(static_cast<double>(a.x) + u[0] * lead),
                         static_cast<float>(static_cast<double>(a.y) + u[1] * lead)};
          pipe.coil_b = {static_cast<float>(static_cast<double>(a.x) + u[0] * (lead + span)),
                         static_cast<float>(static_cast<double>(a.y) + u[1] * (lead + span))};
          const std::vector<Vec2> coil = CoilPoints(pipe.coil_a, pipe.coil_b, pipe.coil_turns);
          line.insert(line.begin() + k + 1, coil.begin(), coil.end());
          coil_points = static_cast<int>(coil.size());
        }
      }
    }
    // An arc runs its segment along a ring of the sigil, the shorter way round: the drops' way gets longer.
    int arc_points = 0;
    if (pipe.arc_segment >= 0) {
      const int k = pipe.arc_segment;
      if (k >= segments || k == pipe.coil_segment || k == pipe.drip_segment) {
        problem("the arc on " + from.id + " -> " + to.id + " needs a segment of the pipe without a coil or a drip");
      } else {
        const int at = coil_points > 0 && k > pipe.coil_segment ? k + coil_points : k;
        const Vec2 a = line[static_cast<size_t>(at)], b = line[static_cast<size_t>(at) + 1];
        const Sigil* sigil = SigilOf(look.sigils, a, b);
        const std::vector<Vec2> arc = sigil ? ArcPoints(*sigil, a, b) : std::vector<Vec2>{};
        if (arc.empty()) {
          problem("the arc on " + from.id + " -> " + to.id +
                  " needs both ends on points of one ring of the sigil (and less than half a ring apart)");
        } else {
          line.insert(line.begin() + at + 1, arc.begin() + 1, arc.end() - 1);
          arc_points = static_cast<int>(arc.size()) - 2;
        }
      }
    }
    // Line point i of the drawing (from, via..., to) in the line with the coil's and the arc's points.
    auto expanded = [&](int i) {
      if (coil_points > 0 && i > pipe.coil_segment) i += coil_points;
      if (arc_points > 0 && i > pipe.arc_segment) i += arc_points;
      return i;
    };
    // Where line point i is, measured along the pipe as drawn (from the edge of its first node).
    auto along_line = [&](int i) {
      float at = 0.0f;
      for (int j = 1; j <= i; ++j) at += Distance(line[static_cast<size_t>(j) - 1], line[static_cast<size_t>(j)]);
      return std::max(0.0f, at - from.radius);
    };
    pipe.coil_start = pipe.coil_end = 0.0f;
    if (coil_points > 0) {
      pipe.coil_start = along_line(expanded(pipe.coil_segment) + 1);
      pipe.coil_end = along_line(expanded(pipe.coil_segment) + coil_points);
    }
    // A drip fall: where its segment starts and ends along the line (look only).
    pipe.drip_start = pipe.drip_end = 0.0f;
    if (pipe.drip_segment >= 0) {
      if (pipe.drip_segment >= segments || pipe.drip_segment == pipe.coil_segment) {
        problem("the drip on " + from.id + " -> " + to.id + " needs a straight segment of the pipe");
      } else {
        pipe.drip_start = along_line(expanded(pipe.drip_segment));
        pipe.drip_end = along_line(expanded(pipe.drip_segment + 1));
      }
    }
    const double exact = ExactLength(line) - static_cast<double>(from.radius) - static_cast<double>(to.radius);
    line = TrimEnd(TrimStart(std::move(line), from.radius), to.radius);
    if (line.size() < 2 || exact <= 0.0) {
      problem("the pipe " + from.id + " -> " + to.id + " is shorter than its nodes");
      line = {from.at, to.at};
    }
    pipe.points = std::move(line);
    pipe.length = PolylineLength(pipe.points);
    pipe.cells = std::max(1, static_cast<int>(std::floor(exact / static_cast<double>(kCellLength) + 0.5)));
  }

  // Crossings: each lane goes on as straight as it can (in i pairs with the out closest to its
  // direction), so lanes follow the drawing.
  for (Node& node : nodes) {
    if (node.kind != NodeKind::Cross) continue;
    auto in_dir = [&](int p) {
      const Pipe& pipe = pipes[static_cast<size_t>(p)];
      return ExactDirection(pipe.via.empty() ? nodes[static_cast<size_t>(pipe.from)].at : pipe.via.back(), node.at);
    };
    auto out_dir = [&](int p) {
      const Pipe& pipe = pipes[static_cast<size_t>(p)];
      return ExactDirection(node.at, pipe.via.empty() ? nodes[static_cast<size_t>(pipe.to)].at : pipe.via.front());
    };
    auto dot = [](std::array<double, 2> a, std::array<double, 2> b) { return a[0] * b[0] + a[1] * b[1]; };
    const double straight =
        dot(in_dir(node.ins[0]), out_dir(node.outs[0])) + dot(in_dir(node.ins[1]), out_dir(node.outs[1]));
    const double swapped =
        dot(in_dir(node.ins[0]), out_dir(node.outs[1])) + dot(in_dir(node.ins[1]), out_dir(node.outs[0]));
    if (swapped > straight) std::swap(node.outs[0], node.outs[1]);
  }

  for (const TrayPiece& tray_piece : tray) {
    if (tray_piece.piece.type == PieceType::Overflow && tray_piece.piece.n < 1) problem("an overflow needs n >= 1");
    if (tray_piece.at < 0) continue;
    if (static_cast<size_t>(tray_piece.at) >= nodes.size()) {
      problem("a placed piece is on a node that doesn't exist");
      continue;
    }
    const Node& node = nodes[static_cast<size_t>(tray_piece.at)];
    if (!node.socket)
      problem(std::string("the ") + PieceName(tray_piece.piece.type) + " can't go on '" + node.id + "'");
  }
  if (CupCount() == 0) problem("no cups");
  return problems;
}

// ---- Geometry ----

std::vector<Vec2> CoilPoints(Vec2 a, Vec2 b, int turns) {
  // sin(k pi / 8) as exact constants, so every device builds the same points (and the same cells).
  static constexpr double kSin[16] = {0.0,  0.38268343236508978,  0.70710678118654757,  0.92387953251128674,
                                      1.0,  0.92387953251128674,  0.70710678118654757,  0.38268343236508978,
                                      0.0,  -0.38268343236508978, -0.70710678118654757, -0.92387953251128674,
                                      -1.0, -0.92387953251128674, -0.70710678118654757, -0.38268343236508978};
  const std::array<double, 2> u = ExactDirection(a, b);
  const std::array<double, 2> v{-u[1], u[0]};
  std::vector<Vec2> points;
  const int samples = std::max(1, turns) * kCoilSamples;
  for (int k = 0; k <= samples; ++k) {
    // theta = -pi/2 + k pi/8: on the axis at both ends; the far side (sin < 0) first.
    const double cos_theta = kSin[k % 16];         // cos(-pi/2 + x) = sin(x)
    const double sin_theta = -kSin[(k + 4) % 16];  // sin(-pi/2 + x) = -cos(x) = -sin(x + pi/2)
    const double along = static_cast<double>(kCoilPitch) * static_cast<double>(k) / kCoilSamples +
                         static_cast<double>(kCoilDepth) * (sin_theta + 1.0);
    const double across = -static_cast<double>(kCoilRadius) * cos_theta;
    points.push_back({static_cast<float>(static_cast<double>(a.x) + u[0] * along + v[0] * across),
                      static_cast<float>(static_cast<double>(a.y) + u[1] * along + v[1] * across)});
  }
  points.back() = b;
  return points;
}

namespace {

// sin(j pi / 48) for a quarter turn (j = 0..24) as exact constants, so every device builds the same arcs (and
// the same cells).
constexpr double kQuarterSin[25] = {0.0,
                                    0.065403129230143062,
                                    0.13052619222005157,
                                    0.19509032201612825,
                                    0.25881904510252074,
                                    0.32143946530316159,
                                    0.38268343236508978,
                                    0.44228869021900125,
                                    0.5,
                                    0.55557023301960218,
                                    0.60876142900872066,
                                    0.65934581510006884,
                                    0.70710678118654757,
                                    0.75183980747897738,
                                    0.79335334029123517,
                                    0.83146961230254512,
                                    0.86602540378443860,
                                    0.89687274153268837,
                                    0.92387953251128674,
                                    0.94693012949510558,
                                    0.96592582628906831,
                                    0.98078528040323043,
                                    0.99144486137381038,
                                    0.99785892323860348,
                                    1.0};
constexpr int kArcSteps = kSigilPoints * kArcSamples;  // a whole ring: 96 steps of 3.75 degrees

double StepSin(int j) {
  j = ((j % kArcSteps) + kArcSteps) % kArcSteps;
  if (j <= 24) return kQuarterSin[j];
  if (j <= 48) return kQuarterSin[48 - j];
  if (j <= 72) return -kQuarterSin[j - 48];
  return -kQuarterSin[96 - j];
}
double StepCos(int j) { return StepSin(j + 24); }

Vec2 RingStep(const Sigil& sigil, int ring, int j) {
  const double r = static_cast<double>(sigil.rings[static_cast<size_t>(ring)]);
  return {static_cast<float>(static_cast<double>(sigil.at.x) + r * StepCos(j)),
          static_cast<float>(static_cast<double>(sigil.at.y) + r * StepSin(j))};
}

}  // namespace

Vec2 SigilPoint(const Sigil& sigil, int ring, int k) { return RingStep(sigil, ring, k * kArcSamples); }

const Sigil* SigilOf(const std::vector<Sigil>& sigils, Vec2 a, Vec2 b) {
  for (const Sigil& sigil : sigils) {
    if (!ArcPoints(sigil, a, b).empty()) return &sigil;
  }
  return nullptr;
}

bool OnSigilPoint(const Sigil& sigil, Vec2 p, int& ring, int& k) {
  for (size_t r = 0; r < sigil.rings.size(); ++r) {
    for (int i = 0; i < kSigilPoints; ++i) {
      const Vec2 q = SigilPoint(sigil, static_cast<int>(r), i);
      if (std::fabs(q.x - p.x) <= kSigilSnap && std::fabs(q.y - p.y) <= kSigilSnap) {
        ring = static_cast<int>(r);
        k = i;
        return true;
      }
    }
  }
  return false;
}

std::vector<Vec2> ArcPoints(const Sigil& sigil, Vec2 a, Vec2 b) {
  int ring_a = 0, ring_b = 0, ka = 0, kb = 0;
  if (!OnSigilPoint(sigil, a, ring_a, ka) || !OnSigilPoint(sigil, b, ring_b, kb) || ring_a != ring_b) return {};
  const int turn = ((kb - ka) % kSigilPoints + kSigilPoints) % kSigilPoints;  // clockwise, in ring points
  if (turn == 0 || turn == kSigilPoints / 2) return {};
  const int way = turn < kSigilPoints / 2 ? 1 : -1;
  const int steps = (way > 0 ? turn : kSigilPoints - turn) * kArcSamples;
  std::vector<Vec2> points{a};
  for (int s = 1; s < steps; ++s) points.push_back(RingStep(sigil, ring_a, ka * kArcSamples + way * s));
  points.push_back(b);
  return points;
}

Vec2 PointAlong(const std::vector<Vec2>& points, float distance) {
  if (points.empty()) return {};
  if (distance <= 0.0f) return points.front();
  for (size_t i = 1; i < points.size(); ++i) {
    const float segment = Distance(points[i - 1], points[i]);
    if (distance <= segment && segment > 0.0f) return Lerp(points[i - 1], points[i], distance / segment);
    distance -= segment;
  }
  return points.back();
}

std::vector<Vec2> RodPoints(const Level& level, int mill, int k) {
  const Node& node = level.nodes[static_cast<size_t>(mill)];
  std::vector<Vec2> points{node.at};
  if (static_cast<size_t>(k) < node.rods.size()) {
    const std::vector<Vec2>& bends = node.rods[static_cast<size_t>(k)];
    points.insert(points.end(), bends.begin(), bends.end());
  }
  points.push_back(level.nodes[static_cast<size_t>(node.gears[static_cast<size_t>(k)])].at);
  return points;
}

Vec2 DropPoint(const Level& level, const Drop& drop, const Loc& loc) {
  if (loc.kind == Loc::OnPipe) {
    const Pipe& pipe = level.pipes[static_cast<size_t>(loc.index)];
    return PointAlong(pipe.points,
                      (static_cast<float>(loc.cell) + 0.5f) * pipe.length / static_cast<float>(pipe.cells));
  }
  if (loc.kind != Loc::InNode && loc.index < 0) return {};
  const Node& node = level.nodes[static_cast<size_t>(loc.index)];
  if (loc.kind != Loc::InNode || node.dwell <= 1) return node.at;
  const bool has_in = drop.lane_in >= 0 && static_cast<size_t>(drop.lane_in) < node.ins.size();
  const bool has_out = drop.lane_out >= 0 && static_cast<size_t>(drop.lane_out) < node.outs.size();
  const Vec2 entry =
      has_in ? level.pipes[static_cast<size_t>(node.ins[static_cast<size_t>(drop.lane_in)])].points.back() : node.at;
  const Vec2 exit = has_out
                        ? level.pipes[static_cast<size_t>(node.outs[static_cast<size_t>(drop.lane_out)])].points.front()
                        : node.at;
  const float a = Distance(entry, node.at), b = Distance(node.at, exit);
  const float along = (static_cast<float>(loc.cell) + 0.5f) / static_cast<float>(node.dwell) * (a + b);
  return along <= a ? Lerp(entry, node.at, a > 0.0f ? along / a : 1.0f)
                    : Lerp(node.at, exit, b > 0.0f ? (along - a) / b : 0.0f);
}

// ---- Board: editing ----

Board::Board(const Level& level) : level_(&level), node_piece_(level.nodes.size(), -1), flaps_(level.nodes.size(), -1) {
  for (const TrayPiece& tray_piece : level.tray) pieces_.push_back({-1, tray_piece.setting});
  ClearPieces();
}

int Board::PieceTypeAt(int node) const {
  const int piece = node_piece_[Index(node)];
  return piece < 0 ? -1 : static_cast<int>(level_->tray[Index(piece)].piece.type);
}

PieceRole Board::RoleOf(int piece) const {
  const int at = pieces_[Index(piece)].at;
  return at < 0 ? PieceRole::Acts : Role(PieceInfo(piece).piece.type, level_->nodes[Index(at)]);
}

bool Board::CanPlace(int piece, int node) const {
  if (piece < 0 || piece >= PieceCount() || node < 0 || Index(node) >= level_->nodes.size()) return false;
  if (Locked(piece)) return false;
  const Node& n = level_->nodes[Index(node)];
  if (!n.socket) return false;
  auto locked_other = [&](int at) {
    const int other = node_piece_[Index(at)];
    return other >= 0 && other != piece && Locked(other);
  };
  if (locked_other(node)) return false;
  if (PieceInfo(piece).piece.type == PieceType::TwinGate && n.twin >= 0 && locked_other(n.twin)) return false;
  return true;
}

bool Board::Place(int piece, int node) {
  if (started_ || !CanPlace(piece, node)) return false;
  Unplace(piece);
  const Node& n = level_->nodes[Index(node)];
  const bool pair = PieceInfo(piece).piece.type == PieceType::TwinGate && n.twin >= 0;
  if (node_piece_[Index(node)] >= 0) Unplace(node_piece_[Index(node)]);
  if (pair && node_piece_[Index(n.twin)] >= 0) Unplace(node_piece_[Index(n.twin)]);
  node_piece_[Index(node)] = piece;
  if (pair) node_piece_[Index(n.twin)] = piece;
  pieces_[Index(piece)].at = node;
  return true;
}

void Board::Unplace(int piece) {
  if (started_ || Locked(piece)) return;
  PieceState& state = pieces_[Index(piece)];
  if (state.at < 0) return;
  node_piece_[Index(state.at)] = -1;
  const int twin = level_->nodes[Index(state.at)].twin;
  if (PieceInfo(piece).piece.type == PieceType::TwinGate && twin >= 0) node_piece_[Index(twin)] = -1;
  state.at = -1;
}

void Board::SetSetting(int piece, int setting) {
  if (started_ || Locked(piece) || !HasSetting(PieceInfo(piece).piece.type)) return;
  pieces_[Index(piece)].setting = setting & 1;
}

bool Board::Routes(int node) const {
  const int type = PieceTypeAt(node);
  return type == static_cast<int>(PieceType::Separator) || type == static_cast<int>(PieceType::Flipper) ||
         type == static_cast<int>(PieceType::Overflow);
}

bool Board::Shared(int node) const {
  const NodeKind kind = level_->nodes[Index(node)].kind;
  if (kind != NodeKind::Cross && kind != NodeKind::Merge) return false;
  // A turnstile lets one drop in at a time, a mixer mixes what it holds, a bridge keeps a crossing's
  // lanes apart. Any other piece leaves the junction shared.
  if (Is(node, PieceType::Turnstile) || Is(node, PieceType::Mixer)) return false;
  return !(kind == NodeKind::Cross && Is(node, PieceType::Bridge));
}

bool Board::FlapInCharge(int node) const { return level_->nodes[Index(node)].kind == NodeKind::Fork && !Routes(node); }

bool Board::FlapTappable(int node) const { return FlapInCharge(node) && level_->nodes[Index(node)].flap_tappable; }

void Board::SetFlap(int node, int side) {
  const Node& n = level_->nodes[Index(node)];
  if (started_ || n.kind != NodeKind::Fork || !n.flap_tappable) return;
  flaps_[Index(node)] = side & 1;
}

int Board::PlacedCount() const {
  int count = 0;
  for (int i = 0; i < PieceCount(); ++i) count += pieces_[Index(i)].at >= 0 && !Locked(i);
  return count;
}

Placement Board::GetPlacement() const {
  Placement placement;
  for (const PieceState& state : pieces_) {
    placement.at.push_back(state.at);
    placement.setting.push_back(state.setting);
  }
  for (size_t n = 0; n < level_->nodes.size(); ++n) {
    const Node& node = level_->nodes[n];
    placement.flap.push_back(node.kind == NodeKind::Fork && node.flap_tappable ? flaps_[n] : -1);
  }
  return placement;
}

void Board::SetPlacement(const Placement& placement) {
  if (started_) return;
  for (int i = 0; i < PieceCount(); ++i) Unplace(i);
  for (int i = 0; i < PieceCount() && Index(i) < placement.at.size(); ++i) {
    if (Locked(i)) continue;
    if (placement.at[Index(i)] >= 0) Place(i, placement.at[Index(i)]);
    if (Index(i) < placement.setting.size()) SetSetting(i, placement.setting[Index(i)]);
  }
  for (size_t n = 0; n < placement.flap.size() && n < level_->nodes.size(); ++n) {
    if (placement.flap[n] >= 0) SetFlap(static_cast<int>(n), placement.flap[n]);
  }
}

void Board::ClearPieces() {
  if (started_) return;
  std::fill(node_piece_.begin(), node_piece_.end(), -1);
  for (int i = 0; i < PieceCount(); ++i) {
    const TrayPiece& tray_piece = level_->tray[Index(i)];
    pieces_[Index(i)] = {-1, tray_piece.setting};
  }
  // Locked pieces first, so a movable one can't take their place.
  for (int pass = 0; pass < 2; ++pass) {
    for (int i = 0; i < PieceCount(); ++i) {
      const TrayPiece& tray_piece = level_->tray[Index(i)];
      if (tray_piece.at < 0 || tray_piece.locked != (pass == 0)) continue;
      const Node& n = level_->nodes[Index(tray_piece.at)];
      node_piece_[Index(tray_piece.at)] = i;
      if (tray_piece.piece.type == PieceType::TwinGate && n.twin >= 0) node_piece_[Index(n.twin)] = i;
      pieces_[Index(i)].at = tray_piece.at;
    }
  }
  for (size_t n = 0; n < level_->nodes.size(); ++n) {
    const Node& node = level_->nodes[n];
    flaps_[n] = node.kind == NodeKind::Fork ? node.flap : -1;
  }
}

// ---- Board: running ----

int Board::NewDrop(const Bead& bead) {
  int slot;
  if (!free_drops_.empty()) {
    slot = free_drops_.back();
    free_drops_.pop_back();
  } else {
    slot = static_cast<int>(drops_.size());
    drops_.emplace_back();
  }
  Drop& drop = drops_[Index(slot)];
  drop = Drop{};
  drop.id = next_id_++;
  drop.bead = bead;
  drop.alive = true;
  drop.moved = true;
  return slot;
}

void Board::Start() {
  const size_t node_count = level_->nodes.size();
  drops_.clear();
  free_drops_.clear();
  cell_offset_.clear();
  int total = 0;
  for (const Pipe& pipe : level_->pipes) {
    cell_offset_.push_back(total);
    total += pipe.cells;
  }
  cells_.assign(Index(total), -1);
  inside_.assign(node_count, {});
  cups_.assign(node_count, {});
  bottle_offset_.assign(node_count, -1);
  int bottles = 0;
  for (size_t n = 0; n < node_count; ++n) {
    if (level_->nodes[n].kind != NodeKind::Rack) continue;
    bottle_offset_[n] = bottles;
    bottles += static_cast<int>(level_->nodes[n].slots.size());
  }
  bottles_.assign(Index(bottles), {});
  gate_open_.assign(node_count, 0);
  for (size_t n = 0; n < node_count; ++n) gate_open_[n] = level_->nodes[n].open ? 1 : 0;
  pending_flips_.assign(node_count, 0);
  mill_turns_.assign(node_count, 0);
  poured_.assign(node_count, 0);
  next_pour_.assign(node_count, 0);
  flipper_next_.assign(node_count, 0);
  overflow_passed_.assign(node_count, 0);
  mixer_slot_.assign(node_count, {-1, -1});
  last_exit_tick_.assign(node_count, -1000);
  last_exit_bead_.assign(node_count, Bead{});
  for (size_t n = 0; n < node_count; ++n) {
    const Node& node = level_->nodes[n];
    if (node.kind == NodeKind::Spout) next_pour_[n] = node.start;
    const int piece = node_piece_[n];
    if (piece >= 0 && PieceInfo(piece).piece.type == PieceType::Flipper)
      flipper_next_[n] = pieces_[Index(piece)].setting;
  }
  events_.clear();
  outcome_ = {};
  tick_ = 0;
  next_id_ = 0;
  started_ = true;
}

void Board::Stop() {
  started_ = false;
  drops_.clear();
  free_drops_.clear();
  events_.clear();
  outcome_ = {};
  tick_ = 0;
}

bool Board::Admits(int node, int lane) const {
  const Node& n = level_->nodes[Index(node)];
  const std::vector<int>& inside = inside_[Index(node)];
  switch (n.kind) {
    case NodeKind::Cup:
    case NodeKind::Drain:
    case NodeKind::Rack: return true;
    case NodeKind::Fork:
    case NodeKind::Pass:
    case NodeKind::Mill: return inside.empty();  // one at a time (a tank holds each drop on its own)
    case NodeKind::Gate: return gate_open_[Index(node)] && inside.empty();
    case NodeKind::Cross:
    case NodeKind::Merge:
      if (Is(node, PieceType::Turnstile)) return inside.empty();
      if (Is(node, PieceType::Mixer)) {
        for (int other : inside) {
          if (drops_[Index(other)].lane_out >= 0) return false;  // still sending out a mixed pair
        }
        return mixer_slot_[Index(node)][Index(lane)] < 0;
      }
      return !Is(node, PieceType::TwinGate);  // a twin gate lets in pairs only (Tick)
    case NodeKind::Spout: return false;
  }
  return false;
}

int Board::Decide(int node, const Drop& drop, int lane) {
  const Node& n = level_->nodes[Index(node)];
  // Without a router: a fork follows its flap, a crossing's lanes go straight on, anything else has
  // one way out.
  const int fallback = n.kind == NodeKind::Fork ? flaps_[Index(node)] : n.kind == NodeKind::Cross ? lane : 0;
  if ((n.kind != NodeKind::Fork && n.kind != NodeKind::Cross) || !Routes(node)) return fallback;
  const int piece = node_piece_[Index(node)];
  const Piece& spec = PieceInfo(piece).piece;
  const int setting = pieces_[Index(piece)].setting;
  switch (spec.type) {
    case PieceType::Separator: {
      const int side_a = setting == 0 ? 0 : 1;
      const Bead& bead = drop.bead;
      if (bead.IsBlend()) {
        if (spec.a != spec.b && bead.Has(spec.a) && bead.Has(spec.b) && bead.a != bead.b) return -2;  // splits
        return fallback;
      }
      if (bead.a == spec.a) return side_a;
      if (bead.a == spec.b) return 1 - side_a;
      return fallback;
    }
    case PieceType::Flipper: {
      const int side = flipper_next_[Index(node)];
      flipper_next_[Index(node)] ^= 1;
      return side;
    }
    case PieceType::Overflow:
      if (overflow_passed_[Index(node)] < spec.n) {
        ++overflow_passed_[Index(node)];
        return setting;
      }
      return 1 - setting;
    default: return fallback;
  }
}

void Board::Land(int d, int node) {
  Drop& drop = drops_[Index(d)];
  const Node& n = level_->nodes[Index(node)];
  Event land{.type = EventType::Land, .node = node, .drop = drop.id, .bead = drop.bead};
  // A cup, or the rack's bottle under the pipe right now.
  CupState* cup = nullptr;
  Color want = Color::None;
  int count = 0, slot = -1;
  if (n.kind == NodeKind::Cup) {
    cup = &cups_[Index(node)];
    want = n.want;
    count = n.count;
  } else if (n.kind == NodeKind::Rack) {
    slot = RackSlot(node);
    cup = &bottles_[Index(bottle_offset_[Index(node)] + slot)];
    want = n.slots[Index(slot)].want;
    count = n.slots[Index(slot)].count;
  }
  if (cup) {
    cup->count += drop.bead.Drops();
    cup->landed_tick = tick_;
    land.value = cup->count;
    land.slot = slot;
    events_.push_back(land);
    const bool pure = drop.bead.a == want && (!drop.bead.IsBlend() || drop.bead.b == want);
    if (!pure && !cup->impure) {
      cup->impure = true;
      Fail(drop.bead.IsMud() ? Failure::Mud : Failure::WrongCup, node, slot);
    }
    if (cup->count > count && !cup->spilled) {
      cup->spilled = true;
      events_.push_back({.type = EventType::Spill, .node = node, .slot = slot});
      Fail(Failure::Spill, node, slot);
    }
    if (cup->count == count && !cup->impure)
      events_.push_back({.type = EventType::CupFull, .node = node, .slot = slot});
  } else {
    events_.push_back(land);
  }
  drop.alive = false;
  drop.loc = {Loc::None, node, 0};
  free_drops_.push_back(d);
}

void Board::EnterNode(int d, int node, int lane) {
  Drop& drop = drops_[Index(d)];
  const Node& n = level_->nodes[Index(node)];
  const int pipe = n.ins[Index(lane)];
  Cell(pipe, level_->pipes[Index(pipe)].cells - 1) = -1;
  drop.moved = true;
  moved_any_ = true;
  events_.push_back({.type = EventType::Enter, .node = node, .pipe = pipe, .drop = drop.id, .bead = drop.bead});
  if (n.kind == NodeKind::Cup || n.kind == NodeKind::Drain || n.kind == NodeKind::Rack) {
    Land(d, node);
    return;
  }
  drop.loc = {Loc::InNode, node, 0};
  drop.entered = tick_;
  drop.lane_in = lane;
  const int piece = PieceTypeAt(node);
  drop.dwell = n.dwell + (piece >= 0 ? TankExtra(static_cast<PieceType>(piece)) : 0);
  drop.dwell_total = drop.dwell;
  drop.lane_out = Decide(node, drop, lane);
  // Different colours through a junction with 0-1 ticks between them: a near miss.
  if (Shared(node) && last_exit_tick_[Index(node)] >= tick_ - 1 && !(last_exit_bead_[Index(node)] == drop.bead) &&
      !drop.bead.IsMud() && !last_exit_bead_[Index(node)].IsMud()) {
    bool alone = true;
    for (int other : inside_[Index(node)]) alone &= drops_[Index(other)].bead == drop.bead;
    if (alone) events_.push_back({.type = EventType::NearMiss, .node = node, .drop = drop.id, .bead = drop.bead});
  }
  inside_[Index(node)].push_back(d);
  if (piece == static_cast<int>(PieceType::Mixer)) {
    // On a fork or a pipe socket the partner's slot never fills: a trap.
    auto& slot = mixer_slot_[Index(node)];
    slot[Index(lane)] = d;
    drop.lane_out = -1;  // waits for its partner
    if (slot[0] >= 0 && slot[1] >= 0) {
      Drop& x = drops_[Index(slot[0])];
      Drop& y = drops_[Index(slot[1])];
      const Color color = x.bead.IsBlend() || y.bead.IsBlend() ? Color::Mud : Mix(x.bead.a, y.bead.a);
      for (Drop* m : {&x, &y}) {
        m->bead = Bead::Pure(color);
        m->dwell = m->dwell_total = n.dwell;
        m->lane_out = n.kind == NodeKind::Cross ? m->lane_in : 0;  // a crossing's pair goes on along its lanes
      }
      slot = {-1, -1};  // the pair is on its way out; the slots hold only drops waiting for a partner
      events_.push_back({.type = EventType::Mix, .node = node, .drop = x.id, .other = y.id, .bead = x.bead});
    }
  }
}

void Board::LeaveOnto(int d, int pipe) {
  Drop& drop = drops_[Index(d)];
  const int node = drop.loc.index;
  std::erase(inside_[Index(node)], d);
  last_exit_tick_[Index(node)] = tick_;
  last_exit_bead_[Index(node)] = drop.bead;
  Cell(pipe, 0) = d;
  drop.loc = {Loc::OnPipe, pipe, 0};
  drop.moved = true;
  moved_any_ = true;
  events_.push_back({.type = EventType::Leave, .node = node, .pipe = pipe, .drop = drop.id, .bead = drop.bead});
  const Node& n = level_->nodes[Index(node)];
  if (n.kind == NodeKind::Mill) {
    // The wheel turns a notch; its gears flip their gates at the end of the tick.
    ++mill_turns_[Index(node)];
    for (int gate : n.gears) ++pending_flips_[Index(gate)];
  }
}

bool Board::TryLeave(int d) {
  Drop& drop = drops_[Index(d)];
  if (drop.dwell > 0 || drop.moved) return false;
  const int node = drop.loc.index;
  const Node& n = level_->nodes[Index(node)];
  if (drop.lane_out == -1) return false;  // a mixer waiting for its partner
  if ((n.kind == NodeKind::Fork || n.kind == NodeKind::Cross) && drop.lane_out == -2) {
    const int piece = node_piece_[Index(node)];
    const Piece& spec = PieceInfo(piece).piece;
    const int side_a = pieces_[Index(piece)].setting == 0 ? 0 : 1;
    const int pipe_a = n.outs[Index(side_a)], pipe_b = n.outs[Index(1 - side_a)];
    if (CellAt(pipe_a, 0) >= 0 || CellAt(pipe_b, 0) >= 0) return false;
    const Loc at = drop.loc;
    const int id = drop.id;
    const int half = NewDrop(Bead::Pure(spec.b));  // may grow drops_: no references across it
    drops_[Index(half)].loc = at;
    drops_[Index(half)].prev = at;
    inside_[Index(node)].push_back(half);
    drops_[Index(d)].bead = Bead::Pure(spec.a);
    events_.push_back({.type = EventType::Split, .node = node, .drop = id, .other = drops_[Index(half)].id});
    drops_[Index(half)].moved = false;
    LeaveOnto(d, pipe_a);
    LeaveOnto(half, pipe_b);
    return true;
  }
  if ((n.kind == NodeKind::Pass || n.kind == NodeKind::Fork) && Is(node, PieceType::TwinGate)) {
    // On one pipe a twin gate waits forever for a partner (a trap).
    if (n.twin < 0) return false;
    // On a twin pair it lets a pair go together: both halves need a drop ready and room ahead.
    int partner = -1;
    for (int other : inside_[Index(n.twin)]) {
      if (drops_[Index(other)].dwell == 0 && !drops_[Index(other)].moved) partner = other;
    }
    const Node& m = level_->nodes[Index(n.twin)];
    if (partner < 0 || CellAt(n.outs[0], 0) >= 0 || CellAt(m.outs[0], 0) >= 0) return false;
    LeaveOnto(d, n.outs[0]);
    LeaveOnto(partner, m.outs[0]);
    return true;
  }
  const int pipe = n.outs[Index(drop.lane_out)];
  if (CellAt(pipe, 0) >= 0) return false;
  LeaveOnto(d, pipe);
  return true;
}

void Board::CheckCollisions() {
  for (size_t n = 0; n < level_->nodes.size(); ++n) {
    if (!Shared(static_cast<int>(n))) continue;
    const std::vector<int>& inside = inside_[n];
    if (inside.size() < 2) continue;
    bool differ = false;
    for (int d : inside) differ |= !(drops_[Index(d)].bead == drops_[Index(inside[0])].bead);
    if (!differ) continue;  // the same colour may share
    bool fresh = false;
    for (int d : inside) {
      Drop& drop = drops_[Index(d)];
      if (drop.bead.IsMud()) continue;
      drop.bead = drop.bead.Muddied();
      fresh = true;
    }
    if (!fresh) continue;
    events_.push_back({.type = EventType::Collide, .node = static_cast<int>(n), .bead = Bead::Pure(Color::Mud)});
    Fail(Failure::Mud, static_cast<int>(n));
  }
}

void Board::Fail(Failure failure, int node, int slot) {
  if (outcome_.failure != Failure::None) return;
  outcome_.failure = failure;
  outcome_.fail_node = node;
  outcome_.fail_slot = slot;
  outcome_.fail_tick = tick_;
}

void Board::Finish(bool jammed) {
  if (!jammed) {
    for (size_t n = 0; n < level_->nodes.size(); ++n) {
      const Node& node = level_->nodes[n];
      if (node.kind == NodeKind::Cup && cups_[n].count < node.count) Fail(Failure::Dry, static_cast<int>(n));
      if (node.kind != NodeKind::Rack) continue;
      for (size_t s = 0; s < node.slots.size(); ++s) {
        if (Bottle(static_cast<int>(n), static_cast<int>(s)).count < node.slots[s].count)
          Fail(Failure::Dry, static_cast<int>(n), static_cast<int>(s));
      }
    }
  } else {
    const JamReport jam = Jam();
    int where = jam.lonely >= 0 ? jam.lonely : !jam.cycle.empty() ? jam.cycle.front() : -1;
    if (where < 0 && !jam.stuck_nodes.empty()) where = jam.stuck_nodes.front();
    Fail(Failure::Jam, where);
  }
  outcome_.over = true;
  outcome_.jammed = jammed;
  outcome_.ticks = tick_;
  outcome_.pieces = PlacedCount();
  outcome_.solved = !jammed && outcome_.failure == Failure::None;
  events_.push_back({.type = jammed ? EventType::Jammed : EventType::Finished, .value = outcome_.solved ? 1 : 0});
}

bool Board::Frozen() const {
  for (const Drop& drop : drops_) {
    if (drop.alive && drop.loc.kind == Loc::InNode && drop.dwell > 0) return false;
  }
  for (size_t n = 0; n < level_->nodes.size(); ++n) {
    const Node& node = level_->nodes[n];
    if (node.kind != NodeKind::Spout) continue;
    if (Index(poured_[n]) < node.beads.size() && CellAt(node.outs[0], 0) < 0) return false;
  }
  return true;
}

bool Board::Waiting(int pipe) const {
  if (!started_) return false;
  const int d = CellAt(pipe, level_->pipes[Index(pipe)].cells - 1);
  return d >= 0 && drops_[Index(d)].waiting;
}

const std::vector<Event>& Board::Tick() {
  events_.clear();
  if (!Running()) return events_;
  ++tick_;
  moved_any_ = false;
  for (size_t n = 0; n < level_->nodes.size(); ++n) {
    const Node& node = level_->nodes[n];
    if (node.kind == NodeKind::Rack && tick_ % node.period == 0) {
      events_.push_back({.type = EventType::Turn, .node = static_cast<int>(n), .slot = RackSlot(static_cast<int>(n))});
    }
  }
  std::vector<bool> was_waiting(drops_.size());
  bool timers = false;
  for (size_t i = 0; i < drops_.size(); ++i) {
    Drop& drop = drops_[i];
    if (!drop.alive) continue;
    was_waiting[i] = drop.waiting;
    drop.prev = drop.loc;
    drop.moved = false;
    drop.waiting = false;
    if (drop.loc.kind == Loc::InNode && drop.dwell > 0) {
      --drop.dwell;
      timers = true;
      const int total = std::max(1, level_->nodes[Index(drop.loc.index)].dwell);
      drop.loc.cell = std::min(total - 1, (drop.dwell_total - drop.dwell) * total / std::max(1, drop.dwell_total));
    }
  }

  // Every move that can happen this tick, until nothing more can: a bead leaving a node or a cell
  // makes room for the one behind it in the same tick, so queues move together and a lock frees
  // on the tick its drop leaves. Contested places go by priority: a turnstile's arrow, then lane
  // order for entries; entry order for exits.
  const size_t node_count = level_->nodes.size();
  for (bool progress = true; progress;) {
    progress = false;
    for (size_t n = 0; n < node_count; ++n) {
      for (size_t k = 0; k < inside_[n].size();) {
        const int d = inside_[n][k];
        if (TryLeave(d)) {
          progress = true;
          k = 0;  // the list changed
          continue;
        }
        ++k;
      }
    }
    for (size_t n = 0; n < node_count; ++n) {
      const Node& node = level_->nodes[n];
      const int lanes = static_cast<int>(node.ins.size());
      if (lanes == 2 && Is(static_cast<int>(n), PieceType::TwinGate)) {
        // A twin gate on a crossing or merge: the front drops of both lanes go in together, or neither.
        const int d0 = Head(node.ins[0]), d1 = Head(node.ins[1]);
        if (d0 < 0 || d1 < 0 || drops_[Index(d0)].moved || drops_[Index(d1)].moved) continue;
        EnterNode(d0, static_cast<int>(n), 0);
        EnterNode(d1, static_cast<int>(n), 1);
        progress = true;
        continue;
      }
      int first = 0;
      if (lanes == 2 && Is(static_cast<int>(n), PieceType::Turnstile)) first = pieces_[Index(node_piece_[n])].setting;
      for (int k = 0; k < lanes; ++k) {
        const int lane = (first + k) % lanes;
        const int d = Head(node.ins[Index(lane)]);
        if (d < 0 || drops_[Index(d)].moved) continue;
        if (!Admits(static_cast<int>(n), lane)) continue;
        EnterNode(d, static_cast<int>(n), lane);
        progress = true;
      }
    }
    for (size_t p = 0; p < level_->pipes.size(); ++p) {
      const int pipe = static_cast<int>(p);
      for (int c = level_->pipes[p].cells - 2; c >= 0; --c) {
        const int d = CellAt(pipe, c);
        if (d < 0 || drops_[Index(d)].moved || CellAt(pipe, c + 1) >= 0) continue;
        Cell(pipe, c) = -1;
        Cell(pipe, c + 1) = d;
        drops_[Index(d)].loc.cell = c + 1;
        drops_[Index(d)].moved = true;
        moved_any_ = true;
        progress = true;
      }
    }
    for (size_t n = 0; n < node_count; ++n) {
      const Node& node = level_->nodes[n];
      if (node.kind != NodeKind::Spout || Index(poured_[n]) >= node.beads.size()) continue;
      if (tick_ < next_pour_[n] || CellAt(node.outs[0], 0) >= 0) continue;
      const int d = NewDrop(node.beads[Index(poured_[n])]);
      Drop& drop = drops_[Index(d)];
      drop.prev = {Loc::InNode, static_cast<int>(n), 0};
      drop.loc = {Loc::OnPipe, node.outs[0], 0};
      Cell(node.outs[0], 0) = d;
      ++poured_[n];
      next_pour_[n] = tick_ + node.beat;
      moved_any_ = true;
      progress = true;
      events_.push_back({.type = EventType::Pour,
                         .node = static_cast<int>(n),
                         .pipe = node.outs[0],
                         .drop = drop.id,
                         .bead = drop.bead});
    }
  }

  // The mills' gears flip their gates now, after every move, so the order of moves inside a tick
  // doesn't matter (two flips of one gate in a tick cancel out).
  for (size_t n = 0; n < node_count; ++n) {
    if (pending_flips_[n] % 2 == 1) {
      gate_open_[n] ^= 1;
      events_.push_back({.type = EventType::Flip, .node = static_cast<int>(n), .value = gate_open_[n]});
    }
    pending_flips_[n] = 0;
  }

  // Who's stuck: a bead on a pipe that didn't move, a bead in a node that's done but can't leave.
  for (size_t i = 0; i < drops_.size(); ++i) {
    Drop& drop = drops_[i];
    if (!drop.alive || drop.moved) continue;
    if (drop.loc.kind == Loc::OnPipe) {
      drop.waiting = true;
      const Pipe& pipe = level_->pipes[Index(drop.loc.index)];
      if (drop.loc.cell == pipe.cells - 1 && !(i < was_waiting.size() && was_waiting[i])) {
        events_.push_back(
            {.type = EventType::Wait, .node = pipe.to, .pipe = drop.loc.index, .drop = drop.id, .bead = drop.bead});
      }
    } else if (drop.loc.kind == Loc::InNode && drop.dwell == 0) {
      drop.waiting = true;
      if (!(i < was_waiting.size() && was_waiting[i])) {
        events_.push_back({.type = EventType::Wait, .node = drop.loc.index, .drop = drop.id, .bead = drop.bead});
      }
    }
  }

  CheckCollisions();
  if (outcome_.over) return events_;
  if (stop_at_failure_ && outcome_.failure != Failure::None) {
    Finish(false);
    return events_;
  }

  bool spouts_done = true;
  for (size_t n = 0; n < node_count; ++n) {
    const Node& node = level_->nodes[n];
    if (node.kind == NodeKind::Spout && Index(poured_[n]) < node.beads.size()) spouts_done = false;
  }
  const bool empty = std::none_of(drops_.begin(), drops_.end(), [](const Drop& drop) { return drop.alive; });
  if (empty && spouts_done) {
    Finish(false);
  } else if ((!moved_any_ && !timers && Frozen()) || tick_ >= kMaxTicks) {
    Finish(true);
  }
  return events_;
}

int Board::RackSlot(int rack) const {
  const Node& n = level_->nodes[Index(rack)];
  if (n.slots.empty()) return 0;
  return (n.first + tick_ / std::max(1, n.period)) % static_cast<int>(n.slots.size());
}

bool Board::GateOpen(int gate) const {
  return started_ ? gate_open_[Index(gate)] != 0 : level_->nodes[Index(gate)].open;
}

int Board::NextArrival(int pipe) const {
  // Breadth first up the pipes: the drop nearest the end of `pipe`, else the nearest one inside or
  // before the node it comes from, and so on.
  std::vector<uint8_t> seen(level_->pipes.size(), 0);
  std::vector<int> frontier{pipe}, next;
  while (!frontier.empty()) {
    next.clear();
    for (int p : frontier) {
      if (seen[Index(p)]) continue;
      seen[Index(p)] = 1;
      for (int c = level_->pipes[Index(p)].cells - 1; c >= 0; --c) {
        if (CellAt(p, c) >= 0) return CellAt(p, c);
      }
      const int from = level_->pipes[Index(p)].from;
      if (!inside_[Index(from)].empty()) return inside_[Index(from)].front();
      for (int in : level_->nodes[Index(from)].ins) next.push_back(in);
    }
    std::swap(frontier, next);
  }
  return -1;
}

JamReport Board::Jam() const {
  JamReport report;
  if (!started_) return report;
  // Each stuck drop waits for one other drop (or for something that never comes): a functional
  // graph, so a circle of waiting is a cycle in it.
  std::vector<int> waits_for(drops_.size(), -1);
  auto add_stuck = [&](int node) {
    if (std::find(report.stuck_nodes.begin(), report.stuck_nodes.end(), node) == report.stuck_nodes.end()) {
      report.stuck_nodes.push_back(node);
    }
  };
  // A partner (or a signal) that may still come: the next drop that would bring it; none = lonely.
  auto partner = [&](size_t i, int node, int pipe) {
    waits_for[i] = pipe >= 0 ? NextArrival(pipe) : -1;
    if (waits_for[i] < 0) report.lonely = node;
  };
  for (size_t i = 0; i < drops_.size(); ++i) {
    const Drop& drop = drops_[i];
    if (!drop.alive) continue;
    if (drop.loc.kind == Loc::OnPipe) {
      const Pipe& pipe = level_->pipes[Index(drop.loc.index)];
      if (drop.loc.cell < pipe.cells - 1) {
        waits_for[i] = CellAt(drop.loc.index, drop.loc.cell + 1);
        continue;
      }
      const int to = pipe.to;
      const Node& n = level_->nodes[Index(to)];
      add_stuck(to);
      const std::vector<int>& inside = inside_[Index(to)];
      if (!inside.empty()) {
        waits_for[i] = inside.front();
      } else if (n.ins.size() == 2 && Is(to, PieceType::TwinGate)) {
        // Waiting at a twin gate on a crossing or merge for a drop in the other lane.
        partner(i, to, n.ins[n.ins[0] == drop.loc.index ? 1 : 0]);
      } else if (n.kind == NodeKind::Gate && !gate_open_[Index(to)]) {
        // Waiting at a shut gate for a drop to turn one of the mills geared to it.
        for (size_t m = 0; m < level_->nodes.size() && waits_for[i] < 0; ++m) {
          const Node& mill = level_->nodes[m];
          if (mill.kind != NodeKind::Mill || std::find(mill.gears.begin(), mill.gears.end(), to) == mill.gears.end())
            continue;
          waits_for[i] = !inside_[m].empty() ? inside_[m].front() : NextArrival(mill.ins[0]);
        }
        if (waits_for[i] < 0) report.lonely = to;
      }
    } else if (drop.loc.kind == Loc::InNode) {
      const int node = drop.loc.index;
      const Node& n = level_->nodes[Index(node)];
      add_stuck(node);
      if (drop.lane_out == -1) {
        // A mixer holding a drop: its partner comes by the other lane, if there is one (a trap otherwise).
        partner(i, node, n.ins.size() == 2 ? n.ins[Index(1 - drop.lane_in)] : -1);
        continue;
      }
      if ((n.kind == NodeKind::Pass || n.kind == NodeKind::Fork) && Is(node, PieceType::TwinGate)) {
        // A twin gate on one pipe is a trap; on a twin pair it waits for the other half's drop, or for
        // room ahead of both.
        if (n.twin < 0) {
          report.lonely = node;
          continue;
        }
        if (inside_[Index(n.twin)].empty()) {
          partner(i, node, level_->nodes[Index(n.twin)].ins[0]);
          continue;
        }
        const int ahead = CellAt(n.outs[0], 0);
        waits_for[i] = ahead >= 0 ? ahead : CellAt(level_->nodes[Index(n.twin)].outs[0], 0);
        continue;
      }
      if (drop.lane_out == -2) {
        for (int p : n.outs) {
          if (CellAt(p, 0) >= 0) waits_for[i] = CellAt(p, 0);
        }
      } else if (drop.lane_out >= 0) {
        waits_for[i] = CellAt(n.outs[Index(drop.lane_out)], 0);
      }
    }
  }
  // Find a cycle: walk from each drop; a walk that comes back to a drop on its own path closes one.
  std::vector<int> state(drops_.size(), 0);  // 0 = new, 1 = on the current walk, 2 = done
  for (size_t start = 0; start < drops_.size() && report.cycle.empty(); ++start) {
    std::vector<int> path;
    int d = static_cast<int>(start);
    while (d >= 0 && state[Index(d)] == 0) {
      state[Index(d)] = 1;
      path.push_back(d);
      d = waits_for[Index(d)];
    }
    if (d >= 0 && state[Index(d)] == 1) {
      auto from = std::find(path.begin(), path.end(), d);
      for (auto it = from; it != path.end(); ++it) {
        const Drop& drop = drops_[Index(*it)];
        int node = -1;
        if (drop.loc.kind == Loc::InNode)
          node = drop.loc.index;
        else if (drop.loc.cell == level_->pipes[Index(drop.loc.index)].cells - 1)
          node = level_->pipes[Index(drop.loc.index)].to;
        if (node >= 0 && (report.cycle.empty() || report.cycle.back() != node) &&
            std::find(report.cycle.begin(), report.cycle.end(), node) == report.cycle.end()) {
          report.cycle.push_back(node);
        }
      }
    }
    for (int p : path) state[Index(p)] = 2;
  }
  return report;
}

}  // namespace threadlock
