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
    {PieceType::TwinGate, "Twin gate", "twin", false},       {PieceType::Candle, "Candle", "candle", false},
    {PieceType::Bucket, "Bucket", "bucket", true},
    {PieceType::Signal, "Signal", "signal", true},        {PieceType::Passing, "Passing place", "passing", false},
};

constexpr const char* kNodeKinds[] = {"spout", "fork",  "pass", "cross", "merge",
                                      "cup",   "drain", "rack", "mill",  "gate", "basin"};
constexpr const char* kLevelTypes[] = {"build", "repair", "spectacle", "challenge", "lab"};
constexpr const char* kFailures[] = {"none", "mud", "wrong_cup", "spill", "dry", "jam", "muddled", "half_full"};
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
PieceRole Role(PieceType type, NodeKind kind, bool twin_pair, bool feeds_cup) {
  using enum PieceType;
  // A candle counts what lands in a cup: it works only on a pipe socket whose way out leads to one.
  if (type == Candle) return kind == NodeKind::Pass && feeds_cup ? PieceRole::Acts : PieceRole::Idle;
  // The canal pieces work on a canal socket (two-way canals).
  if (type == Signal || type == Passing) return kind == NodeKind::Pass ? PieceRole::Acts : PieceRole::Idle;
  // A bucket fills and tips wherever there's one way out per dose: a fork, a pipe, a merge (not a crossing's lanes).
  if (type == Bucket) return IsSocketKind(kind) && kind != NodeKind::Cross ? PieceRole::Acts : PieceRole::Idle;
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

// A cup's wants per colour: a prescription as given, or one colour's count (then want/count stay as given). With a
// prescription, count = the sum and want = its first colour.
static void FillWants(Wants& wants, Color& want, int& count) {
  int sum = 0;
  for (uint8_t w : wants) sum += w;
  if (sum == 0) {
    if (count > 0 && want != Color::None) wants[static_cast<size_t>(want)] = static_cast<uint8_t>(std::min(count, 255));
    return;
  }
  count = sum;
  for (size_t c = 0; c < wants.size(); ++c) {
    if (wants[c] > 0) {
      want = static_cast<Color>(c);
      break;
    }
  }
}

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
      case NodeKind::Basin: node.dwell = 1, node.radius = 20.0f; break;
    }
    // Two-way canals: every place takes a barge for one tick.
    if (two_way && node.dwell > 1) node.dwell = 1;
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
        if (!node.recipe.empty()) {
          for (Color layer : node.recipe) {
            if (layer == Color::None || layer == Color::Mud || layer == Color::Ink)
              problem("cup '" + node.id + "' has a recipe layer that isn't a liquid");
          }
          node.count = static_cast<int>(node.recipe.size());
          node.want = node.recipe.front();
        } else {
          FillWants(node.wants, node.want, node.count);
        }
        if (node.count < 1 || node.want == Color::None) problem("cup '" + node.id + "' wants nothing");
        break;
      case NodeKind::Drain: expect(1, 0); break;
      case NodeKind::Rack:
        expect(1, 0);
        if (node.slots.empty()) problem("rack '" + node.id + "' has no bottles");
        for (Slot& slot : node.slots) {
          FillWants(slot.wants, slot.want, slot.count);
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
      case NodeKind::Basin:
        if (!two_way) problem("basin '" + node.id + "' needs two-way canals");
        if (ins + outs < 1) problem("basin '" + node.id + "' joins no canals");
        if (node.berths < 1 || node.berths > 4) problem("basin '" + node.id + "' needs 1-4 berths");
        break;
    }
    if (two_way && (node.kind == NodeKind::Fork || node.kind == NodeKind::Merge || node.kind == NodeKind::Drain ||
                    node.kind == NodeKind::Rack))
      problem("node '" + node.id + "': two-way canals meet in basins (no forks, merges, drains or racks)");
    if (node.to >= 0 && (node.kind != NodeKind::Spout || static_cast<size_t>(node.to) >= nodes.size() ||
                         nodes[static_cast<size_t>(node.to)].kind != NodeKind::Cup))
      problem("node '" + node.id + "' sails to something that isn't a harbour");
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

  // Pipe sockets that lead straight to a cup (through passes, gates, mills and merges): where a candle acts.
  for (Node& node : nodes) {
    node.feeds_cup = -1;
    if (node.kind != NodeKind::Pass) continue;
    const Node* at = &node;
    for (size_t guard = 0; guard <= nodes.size() && at->outs.size() == 1; ++guard) {
      const int next = pipes[static_cast<size_t>(at->outs[0])].to;
      const Node& to = nodes[static_cast<size_t>(next)];
      if (to.kind == NodeKind::Cup) {
        node.feeds_cup = next;
        break;
      }
      if (to.kind != NodeKind::Pass && to.kind != NodeKind::Gate && to.kind != NodeKind::Mill &&
          to.kind != NodeKind::Merge)
        break;
      at = &to;
    }
  }

  for (const TrayPiece& tray_piece : tray) {
    if (tray_piece.piece.type == PieceType::Overflow && tray_piece.piece.n < 1) problem("an overflow needs n >= 1");
    if (tray_piece.piece.type == PieceType::Candle && (tray_piece.piece.n < 1 || tray_piece.piece.n > 4))
      problem("a candle needs n 1-4");
    if (tray_piece.piece.type == PieceType::Bucket && (tray_piece.piece.n < 2 || tray_piece.piece.n > 4))
      problem("a bucket needs n 2-4");
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
         type == static_cast<int>(PieceType::Overflow) ||
         (type == static_cast<int>(PieceType::Bucket) && level_->nodes[Index(node)].kind == NodeKind::Fork);
}

bool Board::BucketActs(int node) const {
  return Is(node, PieceType::Bucket) && Role(PieceType::Bucket, level_->nodes[Index(node)]) == PieceRole::Acts;
}

bool Board::Shared(int node) const {
  if (two_way_) return false;  // barges wait instead of mixing
  const NodeKind kind = level_->nodes[Index(node)].kind;
  if (kind != NodeKind::Cross && kind != NodeKind::Merge) return false;
  // A turnstile lets one drop in at a time, a mixer mixes what it holds, a bridge keeps a crossing's
  // lanes apart. Any other piece leaves the junction shared.
  if (Is(node, PieceType::Turnstile) || Is(node, PieceType::Mixer)) return false;
  return !(kind == NodeKind::Cross && Is(node, PieceType::Bridge));
}

bool Board::CandleActs(int node) const {
  return Is(node, PieceType::Candle) && Role(PieceType::Candle, level_->nodes[Index(node)]) == PieceRole::Acts;
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
  candle_open_.assign(node_count, 0);
  bucket_pouring_.assign(node_count, 0);
  bucket_side_.assign(node_count, 0);
  last_exit_tick_.assign(node_count, -1000);
  last_exit_bead_.assign(node_count, Bead{});
  for (size_t n = 0; n < node_count; ++n) {
    const Node& node = level_->nodes[n];
    if (node.kind == NodeKind::Spout) next_pour_[n] = node.start;
    const int piece = node_piece_[n];
    if (piece >= 0 && PieceInfo(piece).piece.type == PieceType::Flipper)
      flipper_next_[n] = pieces_[Index(piece)].setting;
    if (piece >= 0 && PieceInfo(piece).piece.type == PieceType::Bucket && node.kind == NodeKind::Fork)
      bucket_side_[n] = static_cast<uint8_t>(pieces_[Index(piece)].setting);
  }
  events_.clear();
  outcome_ = {};
  tick_ = 0;
  next_id_ = 0;
  started_ = true;
  two_way_ = level_->two_way;
  if (two_way_) PrepareTwoWay();
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
  if (BucketActs(node)) {
    // A bucket takes drops until it holds its dose, and nothing while it pours.
    return !bucket_pouring_[Index(node)] && static_cast<int>(inside.size()) < PieceInfo(node_piece_[Index(node)]).piece.n;
  }
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
  int count = 0, slot = -1;
  const Wants* wants = nullptr;
  if (n.kind == NodeKind::Cup) {
    cup = &cups_[Index(node)];
    count = n.count;
    wants = &n.wants;
  } else if (n.kind == NodeKind::Rack) {
    slot = RackSlot(node);
    cup = &bottles_[Index(bottle_offset_[Index(node)] + slot)];
    count = n.slots[Index(slot)].count;
    wants = &n.slots[Index(slot)].wants;
  }
  if (cup) {
    bool over = false;
    cup->count += drop.bead.Drops();
    cup->landed_tick = tick_;
    land.value = cup->count;
    land.slot = slot;
    events_.push_back(land);
    if (!n.recipe.empty()) {
      // A recipe takes its layers in order, one at a time: the layer this drop lands as must be the next one.
      // A blend is two layers at once, so it's always out of turn.
      const int layer = cup->count - drop.bead.Drops();
      const bool in_turn = !drop.bead.IsBlend() && layer < count && drop.bead.a == n.recipe[Index(layer)];
      if (!in_turn && layer < count && !cup->impure) {
        cup->impure = true;
        if (drop.bead.IsMud()) {
          Fail(Failure::Mud, node, slot);
        } else {
          events_.push_back({.type = EventType::Muddle, .node = node, .drop = drop.id, .bead = drop.bead, .value = layer});
          Fail(Failure::Muddled, node, slot);
          if (outcome_.failure == Failure::Muddled && outcome_.fail_tick == tick_ && outcome_.fail_node == node) {
            outcome_.fail_layer = layer;
            outcome_.fail_bead = drop.bead;
          }
        }
      }
    } else {
      // A prescription: so many of each colour (a one-colour cup wants one). A blend counts one of each of its
      // colours. A colour it doesn't want is wrong; one more of a colour than it wants spills.
      bool wrong = false;
      for (Color c : {drop.bead.a, drop.bead.b}) {
        if (c == Color::None) continue;
        const size_t k = static_cast<size_t>(c);
        ++cup->got[k];
        if ((*wants)[k] == 0)
          wrong = true;
        else if (cup->got[k] > (*wants)[k])
          over = true;
      }
      if (wrong && !cup->impure) {
        cup->impure = true;
        Fail(drop.bead.IsMud() ? Failure::Mud : Failure::WrongCup, node, slot);
      }
    }
    if ((cup->count > count || over) && !cup->spilled) {
      cup->spilled = true;
      events_.push_back({.type = EventType::Spill, .node = node, .slot = slot});
      Fail(Failure::Spill, node, slot);
    }
    if (cup->count == count && !cup->impure && !cup->spilled)
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
  if (BucketActs(node)) {
    // Into the bucket's glass: it holds every drop until it's full, then tips them all out of one side.
    drop.dwell = drop.dwell_total = 0;
    drop.lane_out = -1;
    std::vector<int>& glass = inside_[Index(node)];
    if (static_cast<int>(glass.size()) >= PieceInfo(node_piece_[Index(node)]).piece.n) {
      bucket_pouring_[Index(node)] = 1;
      const int side = n.kind == NodeKind::Fork ? bucket_side_[Index(node)] : 0;
      for (int held : glass) drops_[Index(held)].lane_out = side;
      events_.push_back({.type = EventType::Tip, .node = node, .drop = drop.id, .value = side});
    }
    return;
  }
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
  if (bucket_pouring_[Index(node)] && inside_[Index(node)].empty()) {
    // The dose is out: the bucket rights itself; on a fork the next dose goes the other way.
    bucket_pouring_[Index(node)] = 0;
    if (n.kind == NodeKind::Fork) bucket_side_[Index(node)] ^= 1;
  }
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
  // A candle holds its drop until its cup holds enough, then stays open.
  const bool candle = n.kind == NodeKind::Pass && CandleActs(node);
  if (candle && !candle_open_[Index(node)]) {
    const int piece = node_piece_[Index(node)];
    if (cups_[Index(n.feeds_cup)].count < PieceInfo(piece).piece.n) return false;
  }
  const int pipe = n.outs[Index(drop.lane_out)];
  if (CellAt(pipe, 0) >= 0) return false;
  if (candle && !candle_open_[Index(node)]) {
    candle_open_[Index(node)] = 1;
    events_.push_back({.type = EventType::Open, .node = node, .drop = drop.id, .bead = drop.bead});
  }
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
    if (two_way_) {
      Fail(Failure::Jam, jam.full_basin >= 0 ? jam.full_basin : jam.stuck_nodes.empty() ? -1 : jam.stuck_nodes.front());
    } else if (jam.half_full >= 0 && jam.lonely < 0 && jam.cycle.empty()) {
      Fail(Failure::HalfFull, jam.half_full);  // the last dose never filled its bucket: never tipped
    } else {
      int where = jam.lonely >= 0 ? jam.lonely : !jam.cycle.empty() ? jam.cycle.front() : -1;
      if (where < 0 && !jam.stuck_nodes.empty()) where = jam.stuck_nodes.front();
      Fail(Failure::Jam, where);
    }
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
  bool pending = false;
  if (two_way_) TickTwoWay(pending);
  for (bool progress = !two_way_; progress;) {
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
      if (!two_way_ && drop.loc.cell == pipe.cells - 1 && !(i < was_waiting.size() && was_waiting[i])) {
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
  } else if ((!moved_any_ && !timers && (two_way_ ? !pending : Frozen())) || tick_ >= kMaxTicks) {
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
  if (two_way_) return JamTwoWay();
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
      if (BucketActs(node) && !bucket_pouring_[Index(node)]) {
        // A bucket filling: it waits for the next drop that could reach it; none can = it never tips.
        for (int in : n.ins) {
          if (waits_for[i] < 0) waits_for[i] = NextArrival(in);
        }
        if (waits_for[i] < 0) report.half_full = node;
        continue;
      }
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
      if (n.kind == NodeKind::Pass && CandleActs(node) && !candle_open_[Index(node)] &&
          cups_[Index(n.feeds_cup)].count < PieceInfo(node_piece_[Index(node)]).piece.n) {
        // A candle waiting for its cup: when nothing moves, nothing more will land there.
        report.lonely = node;
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


// ---- Two-way canals (World 3, round 4) ----
//
// Every pipe runs both ways, one barge a cell. A barge sails to its harbour by the fewest cells (fixed ties: the lower
// pipe index), moves a cell when the cell ahead is free, and enters a node when the node has room. Two barges facing one
// free cell from both sides both stop (a standoff). Basins hold berths and let any barge whose way is clear leave; a
// crossing holds one (a bridge: one per lane); a canal socket one (a passing place: one from each side). A signal lets
// barges onto its stretch only while none on it heads the other way. Nothing mixes. Integers only.

namespace {
bool StretchEnd(NodeKind kind) { return kind != NodeKind::Pass && kind != NodeKind::Mill && kind != NodeKind::Gate; }
int Other(const Pipe& pipe, int node) { return pipe.from == node ? pipe.to : pipe.from; }
}  // namespace

void Board::PrepareTwoWay() {
  const Level& level = *level_;
  const size_t pipe_count = level.pipes.size();
  // The pipes at each node.
  std::vector<std::vector<int>> at(level.nodes.size());
  for (size_t p = 0; p < pipe_count; ++p) {
    at[Index(level.pipes[p].from)].push_back(static_cast<int>(p));
    at[Index(level.pipes[p].to)].push_back(static_cast<int>(p));
  }
  // Stretches: from each end node along each of its pipes, through sockets, mills and gates, to the next end.
  stretches_.clear();
  stretch_of_.assign(pipe_count, -1);
  for (size_t v = 0; v < level.nodes.size(); ++v) {
    if (!StretchEnd(level.nodes[v].kind)) continue;
    for (int first : at[v]) {
      if (stretch_of_[Index(first)] >= 0) continue;
      Stretch stretch;
      stretch.ends[0] = static_cast<int>(v);
      int node = static_cast<int>(v), pipe = first;
      for (size_t guard = 0; guard <= pipe_count; ++guard) {
        stretch.pipes.push_back(pipe);
        stretch_of_[Index(pipe)] = static_cast<int>(stretches_.size());
        const int next = Other(level.pipes[Index(pipe)], node);
        if (StretchEnd(level.nodes[Index(next)].kind)) {
          stretch.ends[1] = next;
          break;
        }
        stretch.nodes.push_back(next);
        int on = -1;
        for (int q : at[Index(next)]) {
          if (q != pipe) on = q;
        }
        if (on < 0 || stretch_of_[Index(on)] >= 0) {
          stretch.ends[1] = next;
          break;
        }
        node = next;
        pipe = on;
      }
      stretches_.push_back(std::move(stretch));
    }
  }
  // Routes: per harbour, the cells to go from heading along pipe p toward one of its ends (index p * 2 + 1 = toward
  // its \`to\`). Bellman-Ford in integers: the same on every device.
  harbours_.clear();
  for (size_t n = 0; n < level.nodes.size(); ++n) {
    if (level.nodes[n].kind == NodeKind::Cup) harbours_.push_back(static_cast<int>(n));
  }
  auto exits = [&](int v, int came) {
    std::vector<int> out;
    const Node& node = level.nodes[Index(v)];
    if (node.kind == NodeKind::Spout || node.kind == NodeKind::Cup) return out;
    if (node.kind == NodeKind::Cross) {
      for (size_t k = 0; k < node.ins.size() && k < node.outs.size(); ++k) {
        if (node.ins[k] == came) out.push_back(node.outs[k]);
        if (node.outs[k] == came) out.push_back(node.ins[k]);
      }
      return out;
    }
    for (int q : at[Index(v)]) {
      if (q != came) out.push_back(q);
    }
    return out;
  };
  way_cost_.assign(harbours_.size(), std::vector<int>(pipe_count * 2, -1));
  for (size_t h = 0; h < harbours_.size(); ++h) {
    std::vector<int>& cost = way_cost_[h];
    for (size_t round = 0; round <= pipe_count * 2 + 2; ++round) {
      bool changed = false;
      for (size_t p = 0; p < pipe_count; ++p) {
        const Pipe& pipe = level.pipes[p];
        for (int toward_to = 0; toward_to < 2; ++toward_to) {
          const int v = toward_to ? pipe.to : pipe.from;
          int best = -1;
          if (v == harbours_[h]) {
            best = 0;
          } else {
            for (int e : exits(v, static_cast<int>(p))) {
              const int c = cost[Index(e) * 2 + (level.pipes[Index(e)].from == v ? 1 : 0)];
              if (c >= 0 && (best < 0 || c + 1 < best)) best = c + 1;
            }
          }
          if (best < 0) continue;
          const int value = pipe.cells + best;
          int& slot = cost[p * 2 + static_cast<size_t>(toward_to)];
          if (slot < 0 || value < slot) {
            slot = value;
            changed = true;
          }
        }
      }
      if (!changed) break;
    }
  }
}

int Board::StretchSignal(int stretch) const {
  for (int node : stretches_[Index(stretch)].nodes) {
    if (Is(node, PieceType::Signal) && level_->nodes[Index(node)].kind == NodeKind::Pass) return node;
  }
  return -1;
}

bool Board::StretchOpen(int stretch, int k) const {
  if (!started_) return true;
  const int so = k == 0 ? 1 : -1;
  for (const Drop& drop : drops_) {
    if (drop.alive && drop.stretch == stretch && drop.so != so) return false;
  }
  return true;
}

int Board::NextPipe(const Drop& drop, int node) const {
  const Level& level = *level_;
  size_t h = 0;
  while (h < harbours_.size() && harbours_[h] != drop.dest) ++h;
  if (h == harbours_.size()) return -1;
  const Node& n = level.nodes[Index(node)];
  int best = -1, best_cost = -1;
  for (size_t p = 0; p < level.pipes.size(); ++p) {
    const Pipe& pipe = level.pipes[p];
    if ((pipe.from != node && pipe.to != node) || static_cast<int>(p) == drop.via) continue;
    if (n.kind == NodeKind::Cross) {  // a crossing: straight on along its lane
      bool lane = false;
      for (size_t k = 0; k < n.ins.size() && k < n.outs.size(); ++k) {
        lane |= (n.ins[k] == drop.via && n.outs[k] == static_cast<int>(p)) ||
                (n.outs[k] == drop.via && n.ins[k] == static_cast<int>(p));
      }
      if (!lane) continue;
    }
    const int c = way_cost_[h][p * 2 + (pipe.from == node ? 1 : 0)];
    if (c >= 0 && (best_cost < 0 || c < best_cost)) {
      best = static_cast<int>(p);
      best_cost = c;
    }
  }
  return best;
}

bool Board::TwoWayAdmits(int node, const Drop& drop, int via) const {
  (void)drop;
  const Node& n = level_->nodes[Index(node)];
  const std::vector<int>& inside = inside_[Index(node)];
  switch (n.kind) {
    case NodeKind::Cup: return true;
    case NodeKind::Basin: return static_cast<int>(inside.size()) < std::max(1, n.berths);
    case NodeKind::Pass:
      if (Is(node, PieceType::Passing)) {
        if (inside.size() >= 2) return false;
        for (int other : inside) {
          if (drops_[Index(other)].via == via) return false;  // one from each side
        }
        return true;
      }
      return inside.empty();
    case NodeKind::Cross:
      if (Is(node, PieceType::Bridge)) {  // two lanes: one barge on each
        auto lane = [&](int pipe) {
          for (size_t k = 0; k < n.ins.size(); ++k) {
            if (n.ins[k] == pipe || n.outs[k] == pipe) return static_cast<int>(k);
          }
          return -1;
        };
        for (int other : inside) {
          if (lane(drops_[Index(other)].via) == lane(via)) return false;
        }
        return true;
      }
      return inside.empty();
    case NodeKind::Mill: return inside.empty();
    case NodeKind::Gate: return gate_open_[Index(node)] && inside.empty();
    default: return false;
  }
}

bool Board::EntryAllowed(const Drop& drop, int pipe, int node) const {
  const int s = stretch_of_[Index(pipe)];
  if (s < 0 || StretchSignal(s) < 0) return true;
  const Stretch& stretch = stretches_[Index(s)];
  if (node != stretch.ends[0] && node != stretch.ends[1]) return true;  // moving along inside it
  const int so = node == stretch.ends[0] ? 1 : -1;
  for (const Drop& other : drops_) {
    if (other.alive && &other != &drop && other.stretch == s && other.so != so) return false;
  }
  return true;
}

void Board::EnterStretch(Drop& drop, int pipe, int node) {
  const int s = stretch_of_[Index(pipe)];
  const Stretch& stretch = stretches_[Index(s)];
  if (node == stretch.ends[0]) {
    drop.stretch = s;
    drop.so = 1;
  } else if (node == stretch.ends[1]) {
    drop.stretch = s;
    drop.so = -1;
  }
}

void Board::TickTwoWay(bool& pending) {
  const Level& level = *level_;
  const size_t node_count = level.nodes.size();
  std::vector<uint8_t> blocked;           // per drop slot: stopped this tick by a standoff
  std::vector<uint8_t> spout_blocked(node_count, 0);
  bool standoff_told = false;
  struct Intent {
    int d = -1;      // drop slot (-1: a spout's next barge)
    int pipe = -1;   // onto this pipe's cell...
    int cell = -1;
    int node = -1;   // ...or into this node
    int from = -1;   // the node it leaves (a spout, a node it's in), -1 = along its pipe
    int prio = 1;
  };
  for (int round = 0; round < 64; ++round) {
    blocked.resize(drops_.size(), 0);
    std::vector<Intent> intents;
    for (size_t i = 0; i < drops_.size(); ++i) {
      const Drop& drop = drops_[i];
      if (!drop.alive || drop.moved || blocked[i]) continue;
      if (drop.loc.kind == Loc::OnPipe) {
        const Pipe& pipe = level.pipes[Index(drop.loc.index)];
        const int next = drop.loc.cell + drop.dir;
        if (next >= 0 && next < pipe.cells)
          intents.push_back({static_cast<int>(i), drop.loc.index, next, -1, -1, 1});
        else
          intents.push_back({static_cast<int>(i), -1, -1, drop.dir > 0 ? pipe.to : pipe.from, -1, 1});
      } else if (drop.loc.kind == Loc::InNode && drop.dwell == 0) {
        const int node = drop.loc.index;
        const int e = NextPipe(drop, node);
        if (e < 0 || !EntryAllowed(drop, e, node)) continue;
        const Pipe& pipe = level.pipes[Index(e)];
        intents.push_back({static_cast<int>(i), e, pipe.from == node ? 0 : pipe.cells - 1, -1, node, 1});
      }
    }
    for (size_t n = 0; n < node_count; ++n) {
      const Node& node = level.nodes[n];
      if (node.kind != NodeKind::Spout || Index(poured_[n]) >= node.beads.size() || spout_blocked[n]) continue;
      if (tick_ < next_pour_[n]) {
        pending = true;
        continue;
      }
      const int e = node.outs[0];
      Drop probe;
      if (!EntryAllowed(probe, e, static_cast<int>(n))) continue;
      intents.push_back({-1, e, 0, -1, static_cast<int>(n), 1});
    }
    // Two barges facing one free cell from both sides: neither moves (a standoff).
    auto heading = [&](const Intent& it) {
      if (it.from >= 0) return level.pipes[Index(it.pipe)].from == it.from ? 1 : -1;
      return drops_[Index(it.d)].dir;
    };
    for (size_t a = 0; a < intents.size(); ++a) {
      for (size_t b = a + 1; b < intents.size(); ++b) {
        const Intent& x = intents[a];
        const Intent& y = intents[b];
        if (x.pipe < 0 || x.pipe != y.pipe || x.cell != y.cell || CellAt(x.pipe, x.cell) >= 0) continue;
        if (heading(x) == heading(y)) continue;  // one behind the other: the first in order goes
        for (const Intent* it : {&x, &y}) {
          if (it->d >= 0) blocked[Index(it->d)] = 1;
          else spout_blocked[Index(it->from)] = 1;
        }
        if (!standoff_told && x.d >= 0 && y.d >= 0) {
          standoff_told = true;
          events_.push_back({.type = EventType::Standoff, .pipe = x.pipe, .drop = drops_[Index(x.d)].id,
                             .other = drops_[Index(y.d)].id});
        }
      }
    }
    // A signal's arrow: a barge entering its stretch from the arrow's end goes first.
    for (Intent& it : intents) {
      if (it.pipe < 0 || it.from < 0) continue;
      const int s = stretch_of_[Index(it.pipe)];
      const int signal = StretchSignal(s);
      if (signal >= 0 && it.from == stretches_[Index(s)].ends[Index(pieces_[Index(node_piece_[Index(signal)])].setting & 1)])
        it.prio = 0;
    }
    std::stable_sort(intents.begin(), intents.end(), [](const Intent& a, const Intent& b) { return a.prio < b.prio; });
    bool any = false;
    for (const Intent& it : intents) {
      if (it.d >= 0 && (blocked[Index(it.d)] || drops_[Index(it.d)].moved)) continue;
      if (it.d < 0 && spout_blocked[Index(it.from)]) continue;
      if (it.pipe >= 0) {
        if (CellAt(it.pipe, it.cell) >= 0) continue;
        int d = it.d;
        if (d < 0) {  // a spout pours its next barge
          const Node& spout = level.nodes[Index(it.from)];
          if (Index(poured_[Index(it.from)]) >= spout.beads.size()) continue;
          Drop probe;
          if (!EntryAllowed(probe, it.pipe, it.from)) continue;
          d = NewDrop(spout.beads[Index(poured_[Index(it.from)])]);
          Drop& drop = drops_[Index(d)];
          drop.prev = {Loc::InNode, it.from, 0};
          drop.dest = spout.to;
          if (drop.dest < 0) {
            for (int h : harbours_) {
              if (level.nodes[Index(h)].wants[static_cast<size_t>(drop.bead.a)] > 0) {
                drop.dest = h;
                break;
              }
            }
          }
          ++poured_[Index(it.from)];
          next_pour_[Index(it.from)] = tick_ + spout.beat;
          events_.push_back({.type = EventType::Pour, .node = it.from, .pipe = it.pipe, .drop = drop.id, .bead = drop.bead});
        } else if (it.from >= 0) {  // leaving a node
          if (!EntryAllowed(drops_[Index(d)], it.pipe, it.from)) continue;
          std::erase(inside_[Index(it.from)], d);
          const Node& n = level.nodes[Index(it.from)];
          events_.push_back({.type = EventType::Leave, .node = it.from, .pipe = it.pipe, .drop = drops_[Index(d)].id,
                             .bead = drops_[Index(d)].bead});
          if (n.kind == NodeKind::Mill) {
            ++mill_turns_[Index(it.from)];
            for (int gate : n.gears) ++pending_flips_[Index(gate)];
          }
        } else {
          Cell(drops_[Index(d)].loc.index, drops_[Index(d)].loc.cell) = -1;
        }
        Drop& drop = drops_[Index(d)];
        const Pipe& pipe = level.pipes[Index(it.pipe)];
        if (it.from >= 0) {
          drop.dir = pipe.from == it.from ? 1 : -1;
          EnterStretch(drop, it.pipe, it.from);
        }
        drop.loc = {Loc::OnPipe, it.pipe, it.cell};
        Cell(it.pipe, it.cell) = d;
        drop.moved = true;
        moved_any_ = true;
        any = true;
      } else {
        Drop& drop = drops_[Index(it.d)];
        const int via = drop.loc.index;
        if (!TwoWayAdmits(it.node, drop, via)) continue;
        Cell(via, drop.loc.cell) = -1;
        const Stretch& stretch = stretches_[Index(stretch_of_[Index(via)])];
        if (it.node == stretch.ends[0] || it.node == stretch.ends[1]) drop.stretch = -1;
        events_.push_back({.type = EventType::Enter, .node = it.node, .pipe = via, .drop = drop.id, .bead = drop.bead});
        drop.moved = true;
        moved_any_ = true;
        any = true;
        if (level.nodes[Index(it.node)].kind == NodeKind::Cup) {
          Land(it.d, it.node);
          continue;
        }
        drop.loc = {Loc::InNode, it.node, 0};
        drop.entered = tick_;
        drop.via = via;
        drop.dwell = drop.dwell_total = std::max(1, level.nodes[Index(it.node)].dwell);
        drop.lane_in = drop.lane_out = -1;
        inside_[Index(it.node)].push_back(it.d);
      }
    }
    if (!any) break;
  }
}

JamReport Board::JamTwoWay() const {
  JamReport report;
  if (!started_) return report;
  auto add = [&](int node) {
    if (node >= 0 && std::find(report.stuck_nodes.begin(), report.stuck_nodes.end(), node) == report.stuck_nodes.end())
      report.stuck_nodes.push_back(node);
  };
  for (size_t n = 0; n < level_->nodes.size(); ++n) {
    if (!inside_[n].empty()) add(static_cast<int>(n));
    if (level_->nodes[n].kind == NodeKind::Basin && static_cast<int>(inside_[n].size()) >= std::max(1, level_->nodes[n].berths) &&
        report.full_basin < 0)
      report.full_basin = static_cast<int>(n);
  }
  for (size_t a = 0; a < drops_.size() && report.standoff_a < 0; ++a) {
    const Drop& x = drops_[a];
    if (!x.alive || x.stretch < 0) continue;
    for (size_t b = a + 1; b < drops_.size(); ++b) {
      const Drop& y = drops_[b];
      if (y.alive && y.stretch == x.stretch && y.so != x.so) {
        report.standoff_a = x.id;
        report.standoff_b = y.id;
        for (const Drop* drop : {&x, &y}) {
          if (drop->loc.kind == Loc::InNode) add(drop->loc.index);
        }
        break;
      }
    }
  }
  return report;
}

}  // namespace threadlock
