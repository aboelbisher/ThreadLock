#include "sigil_look.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace threadlock::sigil {
namespace {

// One straight piece of a groove: from a to b, part `segment` of groove `line`.
struct Stretch {
  Vec2 a, b;
  int line = 0;
  int segment = 0;
};

float Distance(Vec2 a, Vec2 b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y)); }

float DistanceToSegment(Vec2 p, Vec2 a, Vec2 b) {
  const float dx = b.x - a.x, dy = b.y - a.y, length2 = dx * dx + dy * dy;
  const float t = length2 > 0.0f ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / length2, 0.0f, 1.0f) : 0.0f;
  return Distance(p, {a.x + dx * t, a.y + dy * t});
}

float CrossOf(Vec2 o, Vec2 a, Vec2 b) { return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x); }

// Two stretches on one line, overlapping by more than a touch.
bool Alongside(const Stretch& s, const Stretch& t) {
  const float length = Distance(s.a, s.b);
  if (length <= 0.0f) return false;
  if (std::fabs(CrossOf(s.a, s.b, t.a)) / length > 1.0f || std::fabs(CrossOf(s.a, s.b, t.b)) / length > 1.0f)
    return false;
  const Vec2 u{(s.b.x - s.a.x) / length, (s.b.y - s.a.y) / length};
  auto along = [&](Vec2 p) { return (p.x - s.a.x) * u.x + (p.y - s.a.y) * u.y; };
  const float t0 = std::min(along(t.a), along(t.b)), t1 = std::max(along(t.a), along(t.b));
  return std::min(length, t1) - std::max(0.0f, t0) > 0.5f;
}

// Where two stretches cross (not just touch end to end in a line); false if they don't.
bool Crossing(const Stretch& s, const Stretch& t, Vec2& at) {
  const float d1 = CrossOf(t.a, t.b, s.a), d2 = CrossOf(t.a, t.b, s.b);
  const float d3 = CrossOf(s.a, s.b, t.a), d4 = CrossOf(s.a, s.b, t.b);
  if ((d1 > 0.0f && d2 > 0.0f) || (d1 < 0.0f && d2 < 0.0f) || (d3 > 0.0f && d4 > 0.0f) || (d3 < 0.0f && d4 < 0.0f))
    return false;
  if (d1 == d2) return false;  // parallel (Alongside tells the overlaps)
  const float k = d1 / (d1 - d2);
  at = {s.a.x + (s.b.x - s.a.x) * k, s.a.y + (s.b.y - s.a.y) * k};
  return true;
}

bool IsJunction(NodeKind kind) { return IsSocketKind(kind); }
bool IsVessel(NodeKind kind) {
  return kind == NodeKind::Spout || kind == NodeKind::Cup || kind == NodeKind::Drain || kind == NodeKind::Rack;
}

}  // namespace

bool IsSigil(const Level& level) { return !level.look.sigil.rings.empty(); }

std::vector<Vec2> GroovePoints(const Level& level, const Pipe& pipe) {
  std::vector<Vec2> points{level.nodes[static_cast<size_t>(pipe.from)].at};
  points.insert(points.end(), pipe.via.begin(), pipe.via.end());
  points.push_back(level.nodes[static_cast<size_t>(pipe.to)].at);
  const int k = pipe.arc_segment;
  if (k >= 0 && static_cast<size_t>(k) + 1 < points.size()) {
    const std::vector<Vec2> arc =
        ArcPoints(level.look.sigil, points[static_cast<size_t>(k)], points[static_cast<size_t>(k) + 1]);
    if (arc.size() > 2) points.insert(points.begin() + k + 1, arc.begin() + 1, arc.end() - 1);
  }
  return points;
}

std::vector<std::string> Problems(const Level& level) {
  std::vector<std::string> problems;
  if (!IsSigil(level)) return problems;
  const Sigil& sigil = level.look.sigil;
  auto id = [&](int node) { return level.nodes[static_cast<size_t>(node)].id; };
  auto groove = [&](int p) {
    const Pipe& pipe = level.pipes[static_cast<size_t>(p)];
    return "groove " + id(pipe.from) + " -> " + id(pipe.to);
  };

  // The sigil on the board.
  const float outer = *std::max_element(sigil.rings.begin(), sigil.rings.end());
  if (sigil.at.x - outer < 0.0f || sigil.at.x + outer > 360.0f || sigil.at.y - outer < 0.0f ||
      sigil.at.y + outer > level.look.height)
    problems.push_back("the sigil is off the board");

  // Junctions on its points, or on the figure's lines: straight between two junctions on its points (every
  // point of a 24-point ring is on some chord, so any chord would allow anything).
  std::vector<Vec2> on_points;  // the junctions on ring points
  for (const Node& node : level.nodes) {
    int ring = 0, k = 0;
    if (IsJunction(node.kind) && OnSigilPoint(sigil, node.at, ring, k)) on_points.push_back(node.at);
  }
  for (const Node& node : level.nodes) {
    if (IsJunction(node.kind)) {
      bool on = std::find(on_points.begin(), on_points.end(), node.at) != on_points.end();
      for (size_t i = 0; i < on_points.size() && !on; ++i) {
        for (size_t j = i + 1; j < on_points.size() && !on; ++j)
          on = DistanceToSegment(node.at, on_points[i], on_points[j]) <= kSigilSnap;
      }
      if (!on) problems.push_back("node " + node.id + " isn't on the sigil's points or lines");
    }
    if (IsVessel(node.kind) && Distance(node.at, sigil.at) <= outer)
      problems.push_back("node " + node.id + " is inside the sigil");
  }

  // A groove runs through no node it doesn't join.
  std::vector<Stretch> stretches;
  for (size_t p = 0; p < level.pipes.size(); ++p) {
    const Pipe& pipe = level.pipes[p];
    const std::vector<Vec2> points = GroovePoints(level, pipe);
    for (size_t k = 0; k + 1 < points.size(); ++k)
      stretches.push_back({points[k], points[k + 1], static_cast<int>(p), static_cast<int>(k)});
    for (size_t n = 0; n < level.nodes.size(); ++n) {
      if (static_cast<int>(n) == pipe.from || static_cast<int>(n) == pipe.to) continue;
      const Node& node = level.nodes[n];
      for (size_t k = 0; k + 1 < points.size(); ++k) {
        if (DistanceToSegment(node.at, points[k], points[k + 1]) < std::max(node.radius, 5.0f)) {
          problems.push_back(groove(static_cast<int>(p)) + " runs through node " + node.id);
          break;
        }
      }
    }
  }

  // Grooves don't run along each other (or themselves), and cross only at a crossing node.
  std::vector<std::pair<int, int>> told;  // groove pairs already reported
  auto tell = [&](int p, int q, const std::string& text) {
    if (std::find(told.begin(), told.end(), std::pair{p, q}) != told.end()) return;
    told.emplace_back(p, q);
    problems.push_back(text);
  };
  auto at_node = [&](Vec2 at, int p, int q) {  // `at` is a node both grooves join
    const Pipe &a = level.pipes[static_cast<size_t>(p)], &b = level.pipes[static_cast<size_t>(q)];
    for (int n : {a.from, a.to}) {
      if (Distance(level.nodes[static_cast<size_t>(n)].at, at) < 0.5f && (n == b.from || n == b.to)) return true;
    }
    return false;
  };
  for (size_t i = 0; i < stretches.size(); ++i) {
    for (size_t j = i + 1; j < stretches.size(); ++j) {
      const Stretch &s = stretches[i], &t = stretches[j];
      const bool same = s.line == t.line;
      if (same && std::abs(s.segment - t.segment) == 1) continue;  // they meet at a bend
      const std::string other = same ? "itself" : groove(t.line);
      if (Alongside(s, t)) {
        tell(s.line, t.line, groove(s.line) + " runs along " + other);
        continue;
      }
      Vec2 at;
      if (!Crossing(s, t, at) || at_node(at, s.line, t.line)) continue;
      tell(s.line, t.line, groove(s.line) + " crosses " + other + " outside a crossing");
    }
  }

  // A twin pair's brass bar (straight from one seal to the other) clear of the other nodes.
  for (size_t n = 0; n < level.nodes.size(); ++n) {
    const Node& node = level.nodes[n];
    if (node.twin <= static_cast<int>(n)) continue;
    const Node& twin = level.nodes[static_cast<size_t>(node.twin)];
    for (const Node& other : level.nodes) {
      if (&other == &node || &other == &twin) continue;
      if (DistanceToSegment(other.at, node.at, twin.at) < std::max(other.radius, 5.0f) + 6.0f)
        problems.push_back("the bar of twin pair " + node.id + " + " + twin.id + " runs over node " + other.id);
    }
  }
  return problems;
}

}  // namespace threadlock::sigil
