#include "canal_look.h"

#include <algorithm>
#include <cmath>

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

}  // namespace

std::vector<District> Districts(const Level& level) {
  std::vector<District> districts;
  for (size_t n = 0; n < level.nodes.size(); ++n) {
    const Node& node = level.nodes[n];
    if (node.kind == NodeKind::Cup && !node.district.empty()) districts.push_back({static_cast<int>(n), node.district});
  }
  return districts;
}

bool IsCanalMap(const Level& level) { return !Districts(level).empty(); }

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

}  // namespace threadlock::canal
