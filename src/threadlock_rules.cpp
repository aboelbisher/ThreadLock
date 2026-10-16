#include "threadlock_rules.h"

#include <algorithm>
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

constexpr const char* kNodeKinds[] = {"spout", "fork", "pass", "cross", "merge", "cup", "drain"};
constexpr const char* kLevelTypes[] = {"build", "repair", "spectacle", "challenge", "daily", "lab"};
constexpr const char* kFailures[] = {"none", "mud", "wrong_cup", "spill", "dry", "jam"};

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

bool Fits(PieceType type, const Node& node) {
  switch (type) {
    case PieceType::Separator:
    case PieceType::Flipper:
    case PieceType::Overflow: return node.kind == NodeKind::Fork;
    case PieceType::Turnstile: return node.kind == NodeKind::Cross || node.kind == NodeKind::Merge;
    case PieceType::Bridge: return node.kind == NodeKind::Cross;
    case PieceType::Mixer: return node.kind == NodeKind::Merge;
    case PieceType::TankShort:
    case PieceType::TankTall: return node.kind == NodeKind::Pass && node.twin < 0;
    case PieceType::TwinGate: return node.kind == NodeKind::Pass && node.twin >= 0;
  }
  return false;
}

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
  return static_cast<int>(
      std::count_if(nodes.begin(), nodes.end(), [](const Node& n) { return n.kind == NodeKind::Cup; }));
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
      case NodeKind::Merge: node.dwell = kJunctionDwell, node.radius = 15.0f; break;
      case NodeKind::Cup: node.dwell = 0, node.radius = 0.0f; break;
      case NodeKind::Drain: node.dwell = 0, node.radius = 14.0f; break;
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
    }
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
  // pars are baked on a PC, the daily puzzle is generated on each phone), so they're measured in
  // doubles with plain sqrt, which IEEE 754 rounds exactly everywhere (std::hypot and float sums
  // differ between math libraries, and a length near half a cell would round differently).
  for (Pipe& pipe : pipes) {
    const Node& from = nodes[static_cast<size_t>(pipe.from)];
    const Node& to = nodes[static_cast<size_t>(pipe.to)];
    std::vector<Vec2> line;
    line.push_back(from.at);
    line.insert(line.end(), pipe.via.begin(), pipe.via.end());
    line.push_back(to.at);
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
    if (!node.socket || !Fits(tray_piece.piece.type, node)) {
      problem(std::string("the ") + PieceName(tray_piece.piece.type) + " can't go on '" + node.id + "'");
    }
  }
  if (CupCount() == 0) problem("no cups");
  return problems;
}

// ---- Geometry ----

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

bool Board::CanPlace(int piece, int node) const {
  if (piece < 0 || piece >= PieceCount() || node < 0 || Index(node) >= level_->nodes.size()) return false;
  if (Locked(piece)) return false;
  const Node& n = level_->nodes[Index(node)];
  if (!n.socket || !Fits(PieceInfo(piece).piece.type, n)) return false;
  auto locked_other = [&](int at) {
    const int other = node_piece_[Index(at)];
    return other >= 0 && other != piece && Locked(other);
  };
  if (locked_other(node)) return false;
  if (PieceInfo(piece).piece.type == PieceType::TwinGate && locked_other(n.twin)) return false;
  return true;
}

bool Board::Place(int piece, int node) {
  if (started_ || !CanPlace(piece, node)) return false;
  Unplace(piece);
  const Node& n = level_->nodes[Index(node)];
  const bool twin = PieceInfo(piece).piece.type == PieceType::TwinGate;
  if (node_piece_[Index(node)] >= 0) Unplace(node_piece_[Index(node)]);
  if (twin && node_piece_[Index(n.twin)] >= 0) Unplace(node_piece_[Index(n.twin)]);
  node_piece_[Index(node)] = piece;
  if (twin) node_piece_[Index(n.twin)] = piece;
  pieces_[Index(piece)].at = node;
  return true;
}

void Board::Unplace(int piece) {
  if (started_ || Locked(piece)) return;
  PieceState& state = pieces_[Index(piece)];
  if (state.at < 0) return;
  node_piece_[Index(state.at)] = -1;
  if (PieceInfo(piece).piece.type == PieceType::TwinGate) node_piece_[Index(level_->nodes[Index(state.at)].twin)] = -1;
  state.at = -1;
}

void Board::SetSetting(int piece, int setting) {
  if (started_ || Locked(piece) || !HasSetting(PieceInfo(piece).piece.type)) return;
  pieces_[Index(piece)].setting = setting & 1;
}

bool Board::FlapTappable(int node) const {
  const Node& n = level_->nodes[Index(node)];
  return n.kind == NodeKind::Fork && n.flap_tappable && node_piece_[Index(node)] < 0;
}

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

bool Board::Admits(int node, int lane, const Drop& drop) const {
  (void)drop;
  const Node& n = level_->nodes[Index(node)];
  const std::vector<int>& inside = inside_[Index(node)];
  switch (n.kind) {
    case NodeKind::Cup:
    case NodeKind::Drain: return true;
    case NodeKind::Fork:
    case NodeKind::Pass: return inside.empty();  // one at a time (a tank holds each drop on its own)
    case NodeKind::Cross: return Is(node, PieceType::Turnstile) ? inside.empty() : true;
    case NodeKind::Merge:
      if (Is(node, PieceType::Turnstile)) return inside.empty();
      if (Is(node, PieceType::Mixer)) {
        for (int other : inside) {
          if (drops_[Index(other)].lane_out >= 0) return false;  // still sending out a mixed pair
        }
        return mixer_slot_[Index(node)][Index(lane)] < 0;
      }
      return true;
    case NodeKind::Spout: return false;
  }
  return false;
}

int Board::Decide(int node, const Drop& drop) {
  const int piece = node_piece_[Index(node)];
  const int flap = flaps_[Index(node)];
  if (piece < 0) return flap;
  const Piece& spec = PieceInfo(piece).piece;
  const int setting = pieces_[Index(piece)].setting;
  switch (spec.type) {
    case PieceType::Separator: {
      const int side_a = setting == 0 ? 0 : 1;
      const Bead& bead = drop.bead;
      if (bead.IsBlend()) {
        if (spec.a != spec.b && bead.Has(spec.a) && bead.Has(spec.b) && bead.a != bead.b) return -2;  // splits
        return flap;
      }
      if (bead.a == spec.a) return side_a;
      if (bead.a == spec.b) return 1 - side_a;
      return flap;
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
    default: return flap;
  }
}

void Board::Land(int d, int node) {
  Drop& drop = drops_[Index(d)];
  const Node& n = level_->nodes[Index(node)];
  Event land{.type = EventType::Land, .node = node, .drop = drop.id, .bead = drop.bead};
  if (n.kind == NodeKind::Cup) {
    CupState& cup = cups_[Index(node)];
    cup.count += drop.bead.Drops();
    cup.landed_tick = tick_;
    land.value = cup.count;
    events_.push_back(land);
    const bool pure = drop.bead.a == n.want && (!drop.bead.IsBlend() || drop.bead.b == n.want);
    if (!pure && !cup.impure) {
      cup.impure = true;
      Fail(drop.bead.IsMud() ? Failure::Mud : Failure::WrongCup, node);
    }
    if (cup.count > n.count && !cup.spilled) {
      cup.spilled = true;
      events_.push_back({.type = EventType::Spill, .node = node});
      Fail(Failure::Spill, node);
    }
    if (cup.count == n.count && !cup.impure) events_.push_back({.type = EventType::CupFull, .node = node});
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
  if (n.kind == NodeKind::Cup || n.kind == NodeKind::Drain) {
    Land(d, node);
    return;
  }
  drop.loc = {Loc::InNode, node, 0};
  drop.entered = tick_;
  drop.lane_in = lane;
  const int piece = PieceTypeAt(node);
  drop.dwell = n.dwell + (piece >= 0 ? TankExtra(static_cast<PieceType>(piece)) : 0);
  drop.dwell_total = drop.dwell;
  switch (n.kind) {
    case NodeKind::Fork: drop.lane_out = Decide(node, drop); break;
    case NodeKind::Cross: drop.lane_out = lane; break;
    default: drop.lane_out = 0; break;
  }
  // Different colours through a junction with 0-1 ticks between them: a near miss.
  const bool shared = (n.kind == NodeKind::Cross || n.kind == NodeKind::Merge) && piece < 0;
  if (shared && last_exit_tick_[Index(node)] >= tick_ - 1 && !(last_exit_bead_[Index(node)] == drop.bead) &&
      !drop.bead.IsMud() && !last_exit_bead_[Index(node)].IsMud()) {
    bool alone = true;
    for (int other : inside_[Index(node)]) alone &= drops_[Index(other)].bead == drop.bead;
    if (alone) events_.push_back({.type = EventType::NearMiss, .node = node, .drop = drop.id, .bead = drop.bead});
  }
  inside_[Index(node)].push_back(d);
  if (piece == static_cast<int>(PieceType::Mixer)) {
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
        m->lane_out = 0;
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
}

bool Board::TryLeave(int d) {
  Drop& drop = drops_[Index(d)];
  if (drop.dwell > 0 || drop.moved) return false;
  const int node = drop.loc.index;
  const Node& n = level_->nodes[Index(node)];
  if (drop.lane_out == -1) return false;  // a mixer waiting for its partner
  if (n.kind == NodeKind::Fork && drop.lane_out == -2) {
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
  if (n.kind == NodeKind::Pass && n.twin >= 0 && Is(node, PieceType::TwinGate)) {
    // A twin gate lets a pair go together: both halves need a drop ready and room ahead.
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
    const Node& node = level_->nodes[n];
    if (node.kind != NodeKind::Cross && node.kind != NodeKind::Merge) continue;
    if (node_piece_[n] >= 0) continue;  // a turnstile lets one in, a bridge keeps them apart, a mixer mixes
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

void Board::Fail(Failure failure, int node) {
  if (outcome_.failure != Failure::None) return;
  outcome_.failure = failure;
  outcome_.fail_node = node;
  outcome_.fail_tick = tick_;
}

void Board::Finish(bool jammed) {
  if (!jammed) {
    for (size_t n = 0; n < level_->nodes.size(); ++n) {
      const Node& node = level_->nodes[n];
      if (node.kind == NodeKind::Cup && cups_[n].count < node.count) Fail(Failure::Dry, static_cast<int>(n));
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
      int first = 0;
      if (lanes == 2 && Is(static_cast<int>(n), PieceType::Turnstile)) first = pieces_[Index(node_piece_[n])].setting;
      for (int k = 0; k < lanes; ++k) {
        const int lane = (first + k) % lanes;
        const int pipe = node.ins[Index(lane)];
        const int d = CellAt(pipe, level_->pipes[Index(pipe)].cells - 1);
        if (d < 0 || drops_[Index(d)].moved) continue;
        if (!Admits(static_cast<int>(n), lane, drops_[Index(d)])) continue;
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

JamReport Board::Jam() const {
  JamReport report;
  if (!started_) return report;
  // Each stuck drop waits for one other drop (or for a partner that never comes): a functional
  // graph, so a circle of waiting is a cycle in it.
  std::vector<int> waits_for(drops_.size(), -1);
  auto add_stuck = [&](int node) {
    if (std::find(report.stuck_nodes.begin(), report.stuck_nodes.end(), node) == report.stuck_nodes.end()) {
      report.stuck_nodes.push_back(node);
    }
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
      add_stuck(pipe.to);
      const std::vector<int>& inside = inside_[Index(pipe.to)];
      if (!inside.empty()) waits_for[i] = inside.front();
    } else if (drop.loc.kind == Loc::InNode) {
      const int node = drop.loc.index;
      const Node& n = level_->nodes[Index(node)];
      add_stuck(node);
      if (drop.lane_out == -1 || (n.kind == NodeKind::Pass && n.twin >= 0 && Is(node, PieceType::TwinGate))) {
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

}  // namespace threadlock
