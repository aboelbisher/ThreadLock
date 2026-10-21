#include "canal_look.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <string>

#include <glm/glm.hpp>

namespace threadlock::canal {
namespace {

// One straight stretch of a canal or a rod: from a to b, part `segment` of line `line`.
struct Stretch {
  Vec2 a, b;
  int line = 0;
  int segment = 0;
  bool Across() const { return a.y == b.y && a.x != b.x; }
  bool Down() const { return a.x == b.x && a.y != b.y; }
};

// Its direction as (-1, 0, 1) per axis; (0, 0) at a slant or for no length.
Vec2 Direction(Vec2 a, Vec2 b) {
  if (a.y == b.y && a.x != b.x) return {a.x < b.x ? 1.0f : -1.0f, 0.0f};
  if (a.x == b.x && a.y != b.y) return {0.0f, a.y < b.y ? 1.0f : -1.0f};
  return {};
}

float Distance(Vec2 a, Vec2 b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y)); }

float DistanceToSegment(Vec2 p, Vec2 a, Vec2 b) {
  const float dx = b.x - a.x, dy = b.y - a.y, length2 = dx * dx + dy * dy;
  const float t = length2 > 0.0f ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / length2, 0.0f, 1.0f) : 0.0f;
  return Distance(p, {a.x + dx * t, a.y + dy * t});
}

// Two parallel stretches side by side: how far they overlap along their way (<= 0: not at all), and how far
// apart they are across it.
bool Alongside(const Stretch& s, const Stretch& t) {
  const bool across = s.Across() && t.Across(), down = s.Down() && t.Down();
  if (!across && !down) return false;
  const float s0 = across ? std::min(s.a.x, s.b.x) : std::min(s.a.y, s.b.y);
  const float s1 = across ? std::max(s.a.x, s.b.x) : std::max(s.a.y, s.b.y);
  const float t0 = across ? std::min(t.a.x, t.b.x) : std::min(t.a.y, t.b.y);
  const float t1 = across ? std::max(t.a.x, t.b.x) : std::max(t.a.y, t.b.y);
  const float gap = across ? std::fabs(s.a.y - t.a.y) : std::fabs(s.a.x - t.a.x);
  return std::min(s1, t1) - std::max(s0, t0) > 0.5f && gap < kCanalWidth;
}

bool IsEnd(Vec2 p, const Stretch& s) { return p == s.a || p == s.b; }

// The box round a stretch, `half` to each side of it.
Box Around(Vec2 a, Vec2 b, float half) {
  return {{std::min(a.x, b.x) - half, std::min(a.y, b.y) - half},
          {std::max(a.x, b.x) + half, std::max(a.y, b.y) + half}};
}

// The way from a to b along an axis, else down.
Vec2 AxisWay(Vec2 a, Vec2 b) {
  const Vec2 d = Direction(a, b);
  return d.x == 0.0f && d.y == 0.0f ? Vec2{0.0f, 1.0f} : d;
}

Vec2 QuarterClockwise(Vec2 u) { return {-u.y, u.x}; }

Frame MakeFrame(Vec2 at, Vec2 u) { return {at, u, QuarterClockwise(u)}; }

// The way a canal leaves a node (its first out) or comes into it (its first in).
Vec2 WayOut(const Level& level, const Node& node) {
  if (node.outs.empty()) return {0.0f, 1.0f};
  const std::vector<Vec2> points = CanalPoints(level, level.pipes[static_cast<size_t>(node.outs[0])]);
  return AxisWay(points[0], points[1]);
}
Vec2 WayIn(const Level& level, const Node& node) {
  if (node.ins.empty()) return {0.0f, 1.0f};
  const std::vector<Vec2> points = CanalPoints(level, level.pipes[static_cast<size_t>(node.ins[0])]);
  return AxisWay(points[points.size() - 2], points.back());
}

// A small fast random sequence (mulberry32), the same on every device: the city's seed is its level's id.
class Random {
 public:
  explicit Random(const std::string& seed) {
    for (const char c : seed) state_ = (state_ ^ static_cast<uint8_t>(c)) * 16777619u;  // FNV-1a
  }
  float Next() {
    state_ += 0x6D2B79F5u;
    uint32_t t = state_;
    t = (t ^ (t >> 15)) * (t | 1u);
    t ^= t + (t ^ (t >> 7)) * (t | 61u);
    return static_cast<float>((t ^ (t >> 14)) >> 8) / 16777216.0f;
  }

 private:
  uint32_t state_ = 2166136261u;
};

}  // namespace

Box Frame::Span(float along0, float along1, float across0, float across1) const {
  const Vec2 a = Point(along0, across0), b = Point(along1, across1);
  return {{std::min(a.x, b.x), std::min(a.y, b.y)}, {std::max(a.x, b.x), std::max(a.y, b.y)}};
}

Lock LockAt(const Level& level, int spout) {
  const Node& node = level.nodes[static_cast<size_t>(spout)];
  Lock lock;
  lock.frame = MakeFrame(node.at, WayOut(level, node));
  lock.chamber = lock.frame.Span(-kLockBack, kLockFront, -kLockHalf, kLockHalf);
  // Back from the chamber to 100 beyond the edge of the board.
  const Vec2 back = lock.frame.Point(-kLockBack, 0.0f), u = lock.frame.u;
  float far = 0.0f;
  if (u.y > 0.0f) far = back.y + 100.0f;
  if (u.y < 0.0f) far = level.look.height + 100.0f - back.y;
  if (u.x > 0.0f) far = back.x + 100.0f;
  if (u.x < 0.0f) far = 460.0f - back.x;
  lock.stub = {{back.x - u.x * far, back.y - u.y * far}, back};
  return lock;
}

Vec2 Harbour::Berth(int k) const {
  const int row = k / kBerthsPerRow, column = k % kBerthsPerRow;
  return frame.Point(25.5f + kBerthRow * static_cast<float>(row), (2.5f - static_cast<float>(column)) * 13.0f);
}

Harbour HarbourAt(const Level& level, int cup) {
  const Node& node = level.nodes[static_cast<size_t>(cup)];
  Harbour harbour;
  harbour.frame = MakeFrame(node.at, WayIn(level, node));
  harbour.rows = std::max(1, (node.count + kBerthsPerRow - 1) / kBerthsPerRow);
  harbour.basin = harbour.frame.Span(-kBasinBack, harbour.Deep(), -kBasinHalf, kBasinHalf);
  // The pill: past the basin's far end when the canal comes in up or down, else under the basin.
  if (harbour.frame.u.x == 0.0f) {
    harbour.pill = harbour.frame.Span(harbour.Deep(), harbour.Deep() + 18.0f, -kBasinHalf, kBasinHalf);
  } else {
    const Box& b = harbour.basin;
    const float middle = (b.min.x + b.max.x) * 0.5f;
    harbour.pill = {{middle - kBasinHalf, b.max.y}, {middle + kBasinHalf, b.max.y + 18.0f}};
  }
  return harbour;
}

Mill MillAt(const Level& level, int mill) {
  const Node& node = level.nodes[static_cast<size_t>(mill)];
  Mill result;
  result.frame = MakeFrame(node.at, node.ins.empty() ? WayOut(level, node) : WayIn(level, node));
  if (!node.gears.empty()) {
    const std::vector<Vec2> rod = RodPoints(level, mill, 0);
    const Vec2 v = Direction(rod[0], rod[1]);
    if (v.x * result.frame.u.x + v.y * result.frame.u.y == 0.0f && (v.x != 0.0f || v.y != 0.0f)) result.frame.w = v;
  }
  result.house = result.frame.Span(-22.0f, 22.0f, -56.0f, -20.0f);
  return result;
}

Gate GateAt(const Level& level, int gate) {
  const Node& node = level.nodes[static_cast<size_t>(gate)];
  Gate result;
  result.frame = MakeFrame(node.at, node.ins.empty() ? WayOut(level, node) : WayIn(level, node));
  // Towards the rod of the mill that works it.
  for (size_t m = 0; m < level.nodes.size(); ++m) {
    const Node& mill = level.nodes[m];
    for (size_t g = 0; g < mill.gears.size(); ++g) {
      if (mill.gears[g] != gate) continue;
      const std::vector<Vec2> rod = RodPoints(level, static_cast<int>(m), static_cast<int>(g));
      const Vec2 w = Direction(node.at, rod[rod.size() - 2]);
      if (w.x * result.frame.u.x + w.y * result.frame.u.y == 0.0f && (w.x != 0.0f || w.y != 0.0f)) result.frame.w = w;
    }
  }
  result.lamp = result.frame.Point(12.0f, 28.0f);
  result.post = result.frame.Span(6.0f, 18.0f, 12.0f, 33.0f);
  return result;
}

std::vector<std::vector<Vec2>> CityCanals(const Level& level) {
  std::vector<std::vector<Vec2>> canals;
  for (const Pipe& pipe : level.pipes) {
    std::vector<Vec2> points = CanalPoints(level, pipe);
    if (level.nodes[static_cast<size_t>(pipe.from)].kind == NodeKind::Spout)
      points.insert(points.begin(), LockAt(level, pipe.from).stub.front());
    canals.push_back(std::move(points));
  }
  return canals;
}

std::vector<Box> KeepOuts(const Level& level) {
  std::vector<Box> boxes;
  for (size_t n = 0; n < level.nodes.size(); ++n) {
    const Node& node = level.nodes[n];
    const int i = static_cast<int>(n);
    if (node.kind == NodeKind::Spout) boxes.push_back(LockAt(level, i).chamber.Grown(3.0f));
    if (node.kind == NodeKind::Cup) {
      const Harbour harbour = HarbourAt(level, i);
      boxes.push_back(harbour.basin.Grown(4.0f));
      boxes.push_back(harbour.pill.Grown(2.0f));
    }
    if (node.kind == NodeKind::Mill) {
      boxes.push_back(MillAt(level, i).house.Grown(5.0f));
      for (size_t g = 0; g < node.gears.size(); ++g) {
        const std::vector<Vec2> rod = RodPoints(level, i, static_cast<int>(g));
        for (size_t k = 0; k + 1 < rod.size(); ++k) boxes.push_back(Around(rod[k], rod[k + 1], 9.0f));
      }
    }
    if (node.kind == NodeKind::Gate) boxes.push_back(GateAt(level, i).post.Grown(3.0f));
  }
  return boxes;
}

City MakeCity(const Level& level) {
  City city;
  Random random(level.id);
  const std::vector<std::vector<Vec2>> canals = CityCanals(level);
  std::vector<Box> keep = KeepOuts(level);
  auto hits = [&](const Box& box) {
    for (const std::vector<Vec2>& canal : canals) {
      for (size_t k = 0; k + 1 < canal.size(); ++k) {
        if (Around(canal[k], canal[k + 1], kRoofClear).Overlaps(box)) return true;
      }
    }
    return std::any_of(keep.begin(), keep.end(), [&](const Box& k) { return k.Overlaps(box); });
  };

  // The city's grid of blocks, a street between them; over the board and a margin round it.
  constexpr float kCell = 38.0f;
  const float x0 = -58.0f, x1 = 420.0f, y0 = -206.0f, y1 = level.look.height + 250.0f;

  // A park: a free block of 2 x 2 cells on the board.
  std::vector<Box> parks;
  for (float gy = y0; gy + 2.0f * kCell < y1; gy += kCell) {
    for (float gx = x0; gx + 2.0f * kCell < x1; gx += kCell) {
      const Box park{{gx + 3.0f, gy + 3.0f}, {gx + 2.0f * kCell - 3.0f, gy + 2.0f * kCell - 3.0f}};
      if (park.min.x >= 0.0f && park.max.x <= 360.0f && park.min.y >= 0.0f && park.max.y <= level.look.height &&
          !hits(park))
        parks.push_back(park);
    }
  }
  if (!parks.empty()) {
    const size_t pick =
        std::min(parks.size() - 1, static_cast<size_t>(random.Next() * static_cast<float>(parks.size())));
    city.park = parks[pick];
    keep.push_back(city.park);
    const float w = city.park.max.x - city.park.min.x - 20.0f, h = city.park.max.y - city.park.min.y - 20.0f;
    for (int i = 0; i < 16; ++i) {
      Tree tree;
      tree.at.x = city.park.min.x + 10.0f + random.Next() * w;
      tree.at.y = city.park.min.y + 10.0f + random.Next() * h;
      tree.r = 6.0f + random.Next() * 6.0f;
      city.trees.push_back(tree);
    }
  }

  // Rooftops: each block whole, or split across or down into two.
  auto add = [&](float x, float y, float w, float h) {
    const Box box{{x, y}, {x + w, y + h}};
    if (w < 9.0f || h < 9.0f || hits(box)) return;
    Roof roof;
    roof.box = box;
    roof.tone = std::min(4, static_cast<int>(random.Next() * 5.0f));
    roof.across = w >= h;
    const float cx = random.Next(), cy = random.Next();
    roof.chimney = {x + 3.0f + cx * (w - 9.0f), y + 3.0f + cy * (h - 9.0f)};
    const int windows = random.Next() < 0.45f ? 1 + static_cast<int>(random.Next() * 2.0f) : 0;
    for (int i = 0; i < windows; ++i) {
      const float wx = random.Next(), wy = random.Next();
      roof.windows.push_back({x + 2.0f + wx * (w - 4.0f), y + 2.0f + wy * (h - 4.0f)});
    }
    city.roofs.push_back(std::move(roof));
  };
  for (float gy = y0; gy < y1; gy += kCell) {
    for (float gx = x0; gx < x1; gx += kCell) {
      const float x = gx + 3.0f + random.Next() * 3.0f, y = gy + 3.0f + random.Next() * 3.0f;
      const float w = 32.0f - random.Next() * 5.0f, h = 32.0f - random.Next() * 5.0f;
      const float cut = random.Next();
      if (cut < 0.35f) {
        const float s = 0.4f + random.Next() * 0.2f;
        add(x, y, w * s - 1.5f, h);
        add(x + w * s + 1.5f, y, w * (1.0f - s) - 1.5f, h);
      } else if (cut < 0.7f) {
        const float t = 0.4f + random.Next() * 0.2f;
        add(x, y, w, h * t - 1.5f);
        add(x, y + h * t + 1.5f, w, h * (1.0f - t) - 1.5f);
      } else {
        add(x, y, w, h);
      }
    }
  }

  // Lamps along the quays: every 46, sides in turn, 16 off the middle; none near a node, a bend, a basin or a
  // lock, or in another canal.
  for (size_t c = 0; c < canals.size(); ++c) {
    const std::vector<Vec2>& canal = canals[c];
    std::vector<float> at{0.0f};
    for (size_t k = 1; k < canal.size(); ++k) at.push_back(at.back() + Distance(canal[k - 1], canal[k]));
    for (float s = 30.0f + 7.0f * static_cast<float>(c); s < at.back() - 20.0f; s += 46.0f) {
      size_t k = 1;
      while (k + 1 < canal.size() && at[k] < s) ++k;
      const Vec2 a = canal[k - 1], b = canal[k];
      const float t = (s - at[k - 1]) / std::max(1e-3f, at[k] - at[k - 1]);
      const Vec2 p{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
      const Vec2 u = AxisWay(a, b);
      const float side = (static_cast<int>(s / 46.0f) + static_cast<int>(c)) % 2 ? 1.0f : -1.0f;
      const Vec2 lamp{p.x - u.y * 16.0f * side, p.y + u.x * 16.0f * side};
      if (lamp.y < y0 || lamp.y > y1) continue;
      bool clear = true;
      for (size_t j = 1; j + 1 < canal.size(); ++j) clear = clear && std::fabs(s - at[j]) >= 18.0f;  // a bend
      for (const Node& node : level.nodes) clear = clear && Distance(node.at, lamp) >= 22.0f;
      for (const Box& box : keep) clear = clear && !box.Contains(lamp);
      for (const std::vector<Vec2>& other : canals) {
        for (size_t j = 0; j + 1 < other.size(); ++j)
          clear = clear && DistanceToSegment(lamp, other[j], other[j + 1]) >= 15.0f;
      }
      if (clear) city.lamps.push_back(lamp);
    }
  }
  return city;
}

std::vector<District> Districts(const Level& level) {
  std::vector<District> districts;
  for (size_t n = 0; n < level.nodes.size(); ++n) {
    const Node& node = level.nodes[n];
    if (node.kind == NodeKind::Cup && !node.district.empty()) districts.push_back({static_cast<int>(n), node.district});
  }
  return districts;
}

bool IsCanalMap(const Level& level) { return !Districts(level).empty(); }

Vec2 Way::PointAt(float distance) const {
  const glm::vec2 p = whole.PointAt(lead + distance * stretch);
  return {p.x, p.y};
}

Vec2 Way::DirectionAt(float distance) const {
  const glm::vec2 d = whole.DirectionAt(lead + distance * stretch);
  return {d.x, d.y};
}

Way WayOf(const Level& level, int pipe) {
  const Pipe& rules = level.pipes[static_cast<size_t>(pipe)];
  const Node& from = level.nodes[static_cast<size_t>(rules.from)];
  const Node& to = level.nodes[static_cast<size_t>(rules.to)];
  std::vector<glm::vec2> points;
  for (const Vec2 p : rules.points) points.push_back({p.x, p.y});
  points = art::RoundCorners(points, kBendRadius, 2.5f);
  Way way;
  const float rounded = art::MakePath(points).Length();
  way.stretch = rules.length > 0.0f ? rounded / rules.length : 1.0f;
  std::vector<glm::vec2> whole;
  if (glm::distance(glm::vec2(from.at.x, from.at.y), points.front()) > 0.01f) whole.push_back({from.at.x, from.at.y});
  whole.insert(whole.end(), points.begin(), points.end());
  if (glm::distance(glm::vec2(to.at.x, to.at.y), points.back()) > 0.01f) whole.push_back({to.at.x, to.at.y});
  way.whole = art::MakePath(std::move(whole));
  way.lead = glm::distance(glm::vec2(from.at.x, from.at.y), points.front());
  return way;
}

std::vector<Way> Ways(const Level& level) {
  std::vector<Way> ways;
  for (size_t p = 0; p < level.pipes.size(); ++p) ways.push_back(WayOf(level, static_cast<int>(p)));
  return ways;
}

std::vector<Vec2> CanalPoints(const Level& level, const Pipe& pipe) {
  std::vector<Vec2> points{level.nodes[static_cast<size_t>(pipe.from)].at};
  points.insert(points.end(), pipe.via.begin(), pipe.via.end());
  points.push_back(level.nodes[static_cast<size_t>(pipe.to)].at);
  return points;
}

std::vector<std::string> Problems(const Level& level) {
  std::vector<std::string> problems;
  auto id = [&](int node) { return level.nodes[static_cast<size_t>(node)].id; };
  auto canal = [&](int p) {
    const Pipe& pipe = level.pipes[static_cast<size_t>(p)];
    return "canal " + id(pipe.from) + " -> " + id(pipe.to);
  };

  // Every harbour serves a district of its own.
  const std::vector<District> districts = Districts(level);
  for (size_t n = 0; n < level.nodes.size(); ++n) {
    const Node& node = level.nodes[n];
    if (node.kind == NodeKind::Cup && node.district.empty())
      problems.push_back("harbour " + node.id + " has no district");
    if (node.kind == NodeKind::Cup && node.district.size() > kMaxDistrictName)
      problems.push_back("harbour " + node.id + "'s district name has more than " + std::to_string(kMaxDistrictName) +
                         " letters");
  }
  for (size_t d = 0; d < districts.size(); ++d) {
    for (size_t e = d + 1; e < districts.size(); ++e) {
      if (districts[d].name == districts[e].name)
        problems.push_back("harbours " + id(districts[d].node) + " and " + id(districts[e].node) + " both serve " +
                           districts[d].name);
    }
  }

  // Each canal: room for its round bends (and its nodes) on every stretch, and no doubling back.
  std::vector<Stretch> stretches;
  for (size_t p = 0; p < level.pipes.size(); ++p) {
    const Pipe& pipe = level.pipes[p];
    const std::vector<Vec2> points = CanalPoints(level, pipe);
    const size_t last = points.size() - 2;
    for (size_t k = 0; k + 1 < points.size(); ++k) {
      stretches.push_back({points[k], points[k + 1], static_cast<int>(p), static_cast<int>(k)});
      const float start = k == 0 ? level.nodes[static_cast<size_t>(pipe.from)].radius : kBendRadius;
      const float end = k == last ? level.nodes[static_cast<size_t>(pipe.to)].radius : kBendRadius;
      if (Distance(points[k], points[k + 1]) < start + end)
        problems.push_back(canal(static_cast<int>(p)) + " has no room to bend (segment " + std::to_string(k) + ")");
      if (k == 0) continue;
      const Vec2 before = Direction(points[k - 1], points[k]), after = Direction(points[k], points[k + 1]);
      if (before.x == -after.x && before.y == -after.y && (before.x != 0.0f || before.y != 0.0f))
        problems.push_back(canal(static_cast<int>(p)) + " doubles back (segment " + std::to_string(k) + ")");
    }
  }

  // Locks (pipe sockets), mills and gates sit on a straight stretch: in and out the same way.
  for (const Node& node : level.nodes) {
    if (node.kind != NodeKind::Pass && node.kind != NodeKind::Mill && node.kind != NodeKind::Gate) continue;
    if (node.ins.size() != 1 || node.outs.size() != 1) continue;
    const std::vector<Vec2> in = CanalPoints(level, level.pipes[static_cast<size_t>(node.ins[0])]);
    const std::vector<Vec2> out = CanalPoints(level, level.pipes[static_cast<size_t>(node.outs[0])]);
    const Vec2 a = Direction(in[in.size() - 2], in.back()), b = Direction(out[0], out[1]);
    if (!(a == b)) problems.push_back("node " + node.id + " isn't on a straight stretch");
  }

  // A canal runs through no node it doesn't join.
  for (size_t p = 0; p < level.pipes.size(); ++p) {
    const Pipe& pipe = level.pipes[p];
    const std::vector<Vec2> points = CanalPoints(level, pipe);
    for (size_t n = 0; n < level.nodes.size(); ++n) {
      if (static_cast<int>(n) == pipe.from || static_cast<int>(n) == pipe.to) continue;
      const Node& node = level.nodes[n];
      for (size_t k = 0; k + 1 < points.size(); ++k) {
        if (DistanceToSegment(node.at, points[k], points[k + 1]) < node.radius + kCanalWidth * 0.5f) {
          problems.push_back(canal(static_cast<int>(p)) + " runs through node " + node.id);
          break;
        }
      }
    }
  }

  // Canals don't run alongside each other (or themselves), and cross only at a crossing node.
  std::vector<std::pair<int, int>> told;  // pipe pairs already reported
  auto tell = [&](int p, int q, const std::string& text) {
    if (std::find(told.begin(), told.end(), std::pair{p, q}) != told.end()) return;
    told.emplace_back(p, q);
    problems.push_back(text);
  };
  auto joined = [&](Vec2 at, int p, int q) {  // `at` is a node both pipes join
    const Pipe &a = level.pipes[static_cast<size_t>(p)], &b = level.pipes[static_cast<size_t>(q)];
    for (int n : {a.from, a.to}) {
      if (level.nodes[static_cast<size_t>(n)].at == at && (n == b.from || n == b.to)) return true;
    }
    return false;
  };
  for (size_t i = 0; i < stretches.size(); ++i) {
    for (size_t j = i + 1; j < stretches.size(); ++j) {
      const Stretch &s = stretches[i], &t = stretches[j];
      const bool same = s.line == t.line;
      if (same && std::abs(s.segment - t.segment) == 1) continue;  // they meet at a bend
      const std::string other = same ? "itself" : canal(t.line);
      if (Alongside(s, t)) {
        tell(s.line, t.line, canal(s.line) + " runs along " + other);
        continue;
      }
      const Stretch* across = s.Across() ? &s : t.Across() ? &t : nullptr;
      const Stretch* down = s.Down() ? &s : t.Down() ? &t : nullptr;
      if (!across || !down || across == down) continue;
      const Vec2 at{down->a.x, across->a.y};
      const bool meet = at.x >= std::min(across->a.x, across->b.x) && at.x <= std::max(across->a.x, across->b.x) &&
                        at.y >= std::min(down->a.y, down->b.y) && at.y <= std::max(down->a.y, down->b.y);
      if (!meet || (IsEnd(at, s) && IsEnd(at, t) && joined(at, s.line, t.line))) continue;
      tell(s.line, t.line, canal(s.line) + " crosses " + other + " outside a crossing");
    }
  }

  // Room for the buildings: a lock's chamber and its canal in from the edge, a harbour's basin and its pill, a
  // mill's house; on the board where they must be, on no canal (but their own) and no node (but their own).
  auto canal_box = [&](int p, size_t k) {
    const std::vector<Vec2> points = CanalPoints(level, level.pipes[static_cast<size_t>(p)]);
    return Around(points[k], points[k + 1], kCanalWidth * 0.5f);
  };
  auto own = [&](int node, int p) {
    const Pipe& pipe = level.pipes[static_cast<size_t>(p)];
    return pipe.from == node || pipe.to == node;
  };
  auto sits = [&](const Box& box, int node, const std::string& what, bool own_canals) {
    for (size_t p = 0; p < level.pipes.size(); ++p) {
      if (!own_canals && own(node, static_cast<int>(p))) continue;
      const size_t stretches_in = CanalPoints(level, level.pipes[p]).size() - 1;
      for (size_t k = 0; k < stretches_in; ++k) {
        if (!canal_box(static_cast<int>(p), k).Overlaps(box)) continue;
        problems.push_back(what + " sits on " + canal(static_cast<int>(p)));
        break;
      }
    }
    for (size_t n = 0; n < level.nodes.size(); ++n) {
      if (static_cast<int>(n) != node && box.Grown(level.nodes[n].radius).Contains(level.nodes[n].at))
        problems.push_back(what + " covers node " + level.nodes[n].id);
    }
  };
  auto on_board = [&](const Box& box) {
    return box.min.x >= 0.0f && box.max.x <= 360.0f && box.min.y >= 0.0f && box.max.y <= level.look.height;
  };
  struct Basin {
    int node;
    Box basin, pill;
  };
  std::vector<Basin> basins;
  for (size_t n = 0; n < level.nodes.size(); ++n) {
    const Node& node = level.nodes[n];
    const int i = static_cast<int>(n);
    if (node.kind == NodeKind::Spout) {
      const Lock lock = LockAt(level, i);
      sits(lock.chamber, i, "the lock " + node.id, false);
      sits(Around(lock.stub[0], lock.stub[1], kCanalWidth * 0.5f - 1.0f), i, "the canal into lock " + node.id, false);
    }
    if (node.kind == NodeKind::Cup) {
      const Harbour harbour = HarbourAt(level, i);
      if (!on_board(harbour.basin) || !on_board(harbour.pill))
        problems.push_back("harbour " + node.id + "'s basin or name is off the board");
      sits(harbour.basin, i, "harbour " + node.id + "'s basin", false);
      for (const Basin& other : basins) {
        if (other.basin.Overlaps(harbour.basin) || other.basin.Overlaps(harbour.pill) ||
            other.pill.Overlaps(harbour.basin) || other.pill.Overlaps(harbour.pill))
          problems.push_back("harbours " + id(other.node) + " and " + node.id + " overlap");
      }
      basins.push_back({i, harbour.basin, harbour.pill});
    }
    if (node.kind == NodeKind::Mill) sits(MillAt(level, i).house, i, "the mill " + node.id + "'s house", true);
  }

  // The rods: along the streets (straight across or up and down), on the board, never along a canal.
  for (size_t m = 0; m < level.nodes.size(); ++m) {
    const Node& mill = level.nodes[m];
    for (size_t g = 0; g < mill.gears.size(); ++g) {
      const std::string rod = "the rod " + mill.id + " -> " + id(mill.gears[g]);
      const std::vector<Vec2> points = RodPoints(level, static_cast<int>(m), static_cast<int>(g));
      for (const Vec2& p : points) {
        if (p.x < 0.0f || p.x > 360.0f || p.y < 0.0f || p.y > level.look.height) {
          problems.push_back(rod + " is off the board");
          break;
        }
      }
      bool along = false;
      for (size_t k = 0; k + 1 < points.size(); ++k) {
        const Stretch s{points[k], points[k + 1], -1, static_cast<int>(k)};
        if (!s.Across() && !s.Down()) {
          problems.push_back(rod + " runs at a slant (segment " + std::to_string(k) + ")");
          continue;
        }
        for (const Stretch& t : stretches) {
          if (along || !Alongside(s, t)) continue;
          problems.push_back(rod + " runs along " + canal(t.line));
          along = true;
        }
      }
    }
  }
  return problems;
}

std::optional<Vec2> PulseAt(const std::vector<Vec2>& rod, float since) {
  if (since < 0.0f || since > 1.0f || rod.size() < 2) return std::nullopt;
  float total = 0.0f;
  for (size_t k = 0; k + 1 < rod.size(); ++k) total += Distance(rod[k], rod[k + 1]);
  const float target = since * total;
  float walked = 0.0f;
  for (size_t k = 0; k + 1 < rod.size(); ++k) {
    const float length = Distance(rod[k], rod[k + 1]);
    if (target <= walked + length) {
      const float t = length > 0.0f ? (target - walked) / length : 0.0f;
      return Vec2{rod[k].x + (rod[k + 1].x - rod[k].x) * t, rod[k].y + (rod[k + 1].y - rod[k].y) * t};
    }
    walked += length;
  }
  return rod.back();
}

float WheelAngle(int turns, float inside) {
  return (static_cast<float>(turns) + std::clamp(inside, 0.0f, 1.0f)) * std::numbers::pi_v<float> / 3.0f;
}

}  // namespace threadlock::canal
