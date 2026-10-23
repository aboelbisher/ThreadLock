#include "sigil_look.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <utility>

#include "canal_look.h"

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

bool IsSigil(const Level& level) { return !level.look.sigils.empty(); }

std::vector<Vec2> GroovePoints(const Level& level, const Pipe& pipe) {
  std::vector<Vec2> points{level.nodes[static_cast<size_t>(pipe.from)].at};
  points.insert(points.end(), pipe.via.begin(), pipe.via.end());
  points.push_back(level.nodes[static_cast<size_t>(pipe.to)].at);
  const int k = pipe.arc_segment;
  if (k >= 0 && static_cast<size_t>(k) + 1 < points.size()) {
    const Vec2 a = points[static_cast<size_t>(k)], b = points[static_cast<size_t>(k) + 1];
    const Sigil* sigil = SigilOf(level.look.sigils, a, b);
    const std::vector<Vec2> arc = sigil ? ArcPoints(*sigil, a, b) : std::vector<Vec2>{};
    if (arc.size() > 2) points.insert(points.begin() + k + 1, arc.begin() + 1, arc.end() - 1);
  }
  return points;
}

std::vector<std::string> Problems(const Level& level) {
  std::vector<std::string> problems;
  if (!IsSigil(level)) return problems;
  auto id = [&](int node) { return level.nodes[static_cast<size_t>(node)].id; };
  auto groove = [&](int p) {
    const Pipe& pipe = level.pipes[static_cast<size_t>(p)];
    return "groove " + id(pipe.from) + " -> " + id(pipe.to);
  };
  auto outer = [](const Sigil& sigil) { return *std::max_element(sigil.rings.begin(), sigil.rings.end()); };

  // Each sigil on the board.
  for (const Sigil& sigil : level.look.sigils) {
    const float r = outer(sigil);
    if (sigil.at.x - r < 0.0f || sigil.at.x + r > 360.0f || sigil.at.y - r < 0.0f || sigil.at.y + r > level.look.height)
      problems.push_back("the sigil is off the board");
  }

  // Junctions on a sigil's points, or on its figure's lines: straight between two junctions on the same sigil's
  // points (every point of a 24-point ring is on some chord, so any chord would allow anything).
  std::vector<std::vector<Vec2>> on_points(level.look.sigils.size());  // per sigil: the junctions on its points
  for (const Node& node : level.nodes) {
    for (size_t s = 0; s < level.look.sigils.size(); ++s) {
      int ring = 0, k = 0;
      if (IsJunction(node.kind) && OnSigilPoint(level.look.sigils[s], node.at, ring, k))
        on_points[s].push_back(node.at);
    }
  }
  for (const Node& node : level.nodes) {
    if (IsJunction(node.kind)) {
      bool on = false;
      for (const std::vector<Vec2>& points : on_points) {
        on = on || std::find(points.begin(), points.end(), node.at) != points.end();
        for (size_t i = 0; i < points.size() && !on; ++i) {
          for (size_t j = i + 1; j < points.size() && !on; ++j)
            on = DistanceToSegment(node.at, points[i], points[j]) <= kSigilSnap;
        }
      }
      if (!on) problems.push_back("node " + node.id + " isn't on the sigil's points or lines");
    }
    if (!IsVessel(node.kind)) continue;
    for (const Sigil& sigil : level.look.sigils) {
      if (Distance(node.at, sigil.at) <= outer(sigil)) problems.push_back("node " + node.id + " is inside the sigil");
    }
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

  // The flasks and phials (and the phials' pills) on the board, clear of the other nodes, grooves and pills.
  std::vector<std::pair<int, Vec2>> pills;
  for (size_t n = 0; n < level.nodes.size(); ++n) {
    const Node& node = level.nodes[n];
    const bool flask = node.kind == NodeKind::Spout, phial = node.kind == NodeKind::Cup;
    if (!flask && !phial) continue;
    const int self = static_cast<int>(n);
    const Vessel v = flask ? FlaskAt(level, self) : PhialAt(level, self);
    const std::string name = (flask ? "flask " : "phial ") + node.id;
    const float neck = flask ? kFlaskNeck : 0.0f;
    bool off = v.centre.x - v.r < 0.0f || v.centre.x + v.r > 360.0f || v.centre.y - v.r - neck < 0.0f ||
               v.centre.y + v.r > level.look.height;
    if (phial) {
      const Vec2 pill = PillAt(level, self);
      off = off || pill.x < 0.0f || pill.x > 360.0f || pill.y < 0.0f || pill.y > level.look.height;
      pills.emplace_back(self, pill);
    }
    if (off) problems.push_back(name + " is off the board");
    for (size_t o = 0; o < level.nodes.size(); ++o) {
      const Node& other = level.nodes[o];
      if (o != n && Distance(other.at, v.centre) < v.r + other.radius + 2.0f)
        problems.push_back(name + " covers node " + other.id);
    }
    for (size_t p = 0; p < level.pipes.size(); ++p) {
      const Pipe& pipe = level.pipes[p];
      if (pipe.from == self || pipe.to == self) continue;
      const std::vector<Vec2> points = GroovePoints(level, pipe);
      for (size_t k = 0; k + 1 < points.size(); ++k) {
        if (DistanceToSegment(v.centre, points[k], points[k + 1]) < v.r) {
          problems.push_back(name + " covers " + groove(static_cast<int>(p)));
          break;
        }
      }
    }
  }
  for (size_t i = 0; i < pills.size(); ++i) {
    for (size_t j = i + 1; j < pills.size(); ++j) {
      const Vec2 a = pills[i].second, b = pills[j].second;
      if (std::fabs(a.x - b.x) < 60.0f && std::fabs(a.y - b.y) < kPillHeight + 2.0f)
        problems.push_back("the pills of " + id(pills[i].first) + " and " + id(pills[j].first) + " overlap");
    }
  }
  return problems;
}

// ---- The sanctum's still picture ----

namespace {

constexpr float kDegrees = 57.2957795f;

Vec2 Unit(Vec2 a, Vec2 b) {
  const float d = Distance(a, b);
  return d > 0.0f ? Vec2{(b.x - a.x) / d, (b.y - a.y) / d} : Vec2{0.0f, 1.0f};
}

// The way a node's groove leaves it (its first out) or arrives (its first in), from its points.
Vec2 WayOut(const Level& level, int node) {
  const Node& n = level.nodes[static_cast<size_t>(node)];
  if (n.outs.empty()) return {0.0f, 1.0f};
  const std::vector<Vec2> points = GroovePoints(level, level.pipes[static_cast<size_t>(n.outs[0])]);
  return Unit(points[0], points[1]);
}
Vec2 WayIn(const Level& level, int node) {
  const Node& n = level.nodes[static_cast<size_t>(node)];
  if (n.ins.empty()) return {0.0f, 1.0f};
  const std::vector<Vec2> points = GroovePoints(level, level.pipes[static_cast<size_t>(n.ins[0])]);
  return Unit(points[points.size() - 2], points.back());
}

// An angle in degrees, 0..360 (0 = +x, clockwise on the board).
float AngleOf(Vec2 centre, Vec2 p) {
  const float a = std::atan2(p.y - centre.y, p.x - centre.x) * kDegrees;
  return a < 0.0f ? a + 360.0f : a;
}
float AngleApart(float a, float b) {
  const float d = std::fmod(std::fabs(a - b), 360.0f);
  return std::min(d, 360.0f - d);
}

bool IsVesselNode(const Node& node) { return node.kind == NodeKind::Spout || node.kind == NodeKind::Cup; }
Vessel VesselOf(const Level& level, int node) {
  return level.nodes[static_cast<size_t>(node)].kind == NodeKind::Spout ? FlaskAt(level, node) : PhialAt(level, node);
}

}  // namespace

Vessel FlaskAt(const Level& level, int spout) {
  const Vec2 at = level.nodes[static_cast<size_t>(spout)].at, u = WayOut(level, spout);
  const float height = level.look.height;
  auto fits = [&](Vec2 c, float r) {
    return c.x - r >= 2.0f && c.x + r <= 358.0f && c.y - r - kFlaskNeck >= 2.0f && c.y + r <= height - 2.0f;
  };
  for (float r = 23.0f; r >= 14.0f; r -= 1.0f) {
    const Vec2 c{at.x - u.x * (r - 7.0f), at.y - u.y * (r - 7.0f)};
    if (fits(c, r)) return {c, r, {0.0f, -1.0f}};
  }
  const float r = 14.0f;
  Vec2 c{at.x - u.x * (r - 7.0f), at.y - u.y * (r - 7.0f)};
  c.x = std::clamp(c.x, r + 2.0f, 358.0f - r);
  c.y = std::clamp(c.y, r + kFlaskNeck + 2.0f, height - r - 2.0f);
  return {c, r, {0.0f, -1.0f}};
}

Vessel PhialAt(const Level& level, int cup) {
  const Node& node = level.nodes[static_cast<size_t>(cup)];
  const Vec2 u = WayIn(level, cup);
  const float r = node.count >= 5 ? 24.0f : 15.0f;
  return {{node.at.x + u.x * (r + 9.0f), node.at.y + u.y * (r + 9.0f)}, r, {-u.x, -u.y}};
}

Vec2 PillAt(const Level& level, int cup) {
  const Vessel phial = PhialAt(level, cup);
  // A phial hanging neck down (fed from below) has its groove under it: the pill goes over it, clear of the glass.
  if (phial.up.y > 0.5f) return {phial.centre.x, phial.centre.y - phial.r - 14.0f};
  const Vec2 below{phial.centre.x, phial.centre.y + phial.r + 22.0f};
  if (below.y <= level.look.height - 2.0f) return below;
  return {phial.centre.x, phial.centre.y - phial.r - kPhialNeck - 14.0f};
}

float GlobeRadius(const Level& level, int node) {
  const Node& n = level.nodes[static_cast<size_t>(node)];
  if (n.twin >= 0) return 15.0f;
  for (const Node& other : level.nodes) {
    if (other.twin == node) return 15.0f;
  }
  return n.socket ? 26.0f : 15.0f;
}

std::vector<float> Ports(const Level& level, const Sigil& sigil, float ring) {
  std::vector<float> ports;
  const Vec2 c = sigil.at;
  for (const Pipe& pipe : level.pipes) {
    const std::vector<Vec2> points = GroovePoints(level, pipe);
    for (size_t k = 0; k + 1 < points.size(); ++k) {
      // |a + t (b - a) - c| = ring, t in 0..1 (a little past either end: a node on the ring is a port).
      const Vec2 a = points[k], b = points[k + 1];
      const float dx = b.x - a.x, dy = b.y - a.y, fx = a.x - c.x, fy = a.y - c.y;
      const float qa = dx * dx + dy * dy, qb = 2.0f * (fx * dx + fy * dy), qc = fx * fx + fy * fy - ring * ring;
      const float disc = qb * qb - 4.0f * qa * qc;
      if (qa <= 0.0f || disc < 0.0f) continue;
      for (const float sign : {-1.0f, 1.0f}) {
        const float t = (-qb + sign * std::sqrt(disc)) / (2.0f * qa);
        if (t < -0.01f || t > 1.01f) continue;
        const float angle = AngleOf(c, {a.x + dx * t, a.y + dy * t});
        if (std::none_of(ports.begin(), ports.end(), [&](float p) { return AngleApart(p, angle) < 1.0f; }))
          ports.push_back(angle);
      }
    }
  }
  return ports;
}

std::vector<Stroke2> Runes(const Level& level, const Sigil& sigil, float ring) {
  std::vector<Stroke2> runes;
  const std::vector<float> ports = Ports(level, sigil, ring);
  const Vec2 c = sigil.at;
  canal::Random random(level.id);
  for (int i = 0; i < 40; ++i) {
    const float a = 9.0f * static_cast<float>(i) + 4.5f;
    if (std::any_of(ports.begin(), ports.end(), [&](float p) { return AngleApart(a, p) < 7.0f; })) continue;
    const float rad = a / kDegrees;
    const Vec2 n{std::cos(rad), std::sin(rad)}, t{-n.y, n.x};
    const Vec2 at{c.x + n.x * (ring - 4.0f), c.y + n.y * (ring - 4.0f)};
    const int strokes = random.Next() < 0.5f ? 2 : 3;
    for (int s = 0; s < strokes; ++s) {
      const float u0 = (random.Next() - 0.5f) * 6.0f;
      const float v0 = (random.Next() - 0.5f) * 3.5f;
      const float u1 = (random.Next() - 0.5f) * 6.0f;
      const float v1 = (random.Next() - 0.5f) * 3.5f;
      runes.push_back({{at.x + t.x * u0 + n.x * v0, at.y + t.y * u0 + n.y * v0},
                       {at.x + t.x * u1 + n.x * v1, at.y + t.y * u1 + n.y * v1}});
    }
  }
  return runes;
}

std::vector<Vec2> Figure(const Level& level, const Sigil& sigil) {
  std::vector<Vec2> figure;
  for (const Node& node : level.nodes) {
    int ring = 0, k = 0;
    if (IsJunction(node.kind) && OnSigilPoint(sigil, node.at, ring, k)) figure.push_back(node.at);
  }
  std::sort(figure.begin(), figure.end(), [&](Vec2 a, Vec2 b) { return AngleOf(sigil.at, a) < AngleOf(sigil.at, b); });
  return figure;
}

std::vector<std::vector<Vec2>> FigureLines(const Level& level, const Sigil& sigil) {
  const std::vector<Vec2> figure = Figure(level, sigil);
  const Vec2 c = sigil.at;
  auto regular = [&](float step) {
    for (size_t i = 0; i < figure.size(); ++i) {
      const float apart = AngleApart(AngleOf(c, figure[i]), AngleOf(c, figure[(i + 1) % figure.size()]));
      if (std::fabs(apart - step) > 2.0f) return false;
    }
    return true;
  };
  if (figure.size() == 3 && regular(120.0f)) {
    std::vector<Vec2> turned;
    for (const Vec2 p : figure) turned.push_back({2.0f * c.x - p.x, 2.0f * c.y - p.y});
    return {figure, turned};
  }
  if (figure.size() == 6 && regular(60.0f))
    return {{figure[0], figure[2], figure[4]}, {figure[1], figure[3], figure[5]}};
  if (figure.size() >= 3) return {figure};
  return {};
}

std::vector<Vec2> Candles(const Level& level) {
  std::vector<Vessel> vessels;
  std::vector<Vec2> pills;
  for (size_t n = 0; n < level.nodes.size(); ++n) {
    if (!IsVesselNode(level.nodes[n])) continue;
    vessels.push_back(VesselOf(level, static_cast<int>(n)));
    if (level.nodes[n].kind == NodeKind::Cup) pills.push_back(PillAt(level, static_cast<int>(n)));
  }
  auto clear = [&](Vec2 p) {
    for (const Vessel& v : vessels) {
      if (Distance(p, v.centre) < v.r + 16.0f) return false;
    }
    for (const Vec2 pill : pills) {
      if (std::fabs(p.x - pill.x) < 46.0f && std::fabs(p.y - pill.y) < 24.0f) return false;  // a pill is ~60 wide
    }
    return true;
  };
  std::vector<Vec2> candles;
  const int screens = std::max(1, static_cast<int>(std::ceil(level.look.height / 600.0f - 0.01f)));
  for (int s = 0; s < screens; ++s) {
    const float dy = 600.0f * static_cast<float>(s);
    for (const Vec2 corner : {Vec2{19, 64}, Vec2{341, 64}, Vec2{11, 618}, Vec2{349, 618}}) {
      if (corner.y > 600.0f && s + 1 < screens) continue;  // the next screen's top pair is just below
      Vec2 at{corner.x, corner.y + dy};
      for (float d = 2.0f; !clear(at) && d <= 30.0f; d += 2.0f) {
        for (const Vec2 way : {Vec2{0, 1}, Vec2{0, -1}, Vec2{1, 0}, Vec2{-1, 0}}) {
          const Vec2 moved{corner.x + way.x * d, corner.y + dy + way.y * d};
          if (clear(moved)) {
            at = moved;
            break;
          }
        }
      }
      if (clear(at)) candles.push_back(at);  // none where nothing near the corner is clear
    }
  }
  return candles;
}

// ---- The moments ----

namespace {

float GrooveLength(const Level& level, int pipe) {
  const std::vector<Vec2> points = GroovePoints(level, level.pipes[static_cast<size_t>(pipe)]);
  float length = 0.0f;
  for (size_t k = 0; k + 1 < points.size(); ++k) length += Distance(points[k], points[k + 1]);
  return length;
}

// The grooves that feed `pipe` where it starts: none from a flask, the lane's own in at a crossing, else every in.
std::vector<int> Upstream(const Level& level, int pipe) {
  const Node& from = level.nodes[static_cast<size_t>(level.pipes[static_cast<size_t>(pipe)].from)];
  if (from.kind == NodeKind::Spout) return {};
  if (from.kind == NodeKind::Cross) {
    const auto k = std::find(from.outs.begin(), from.outs.end(), pipe) - from.outs.begin();
    if (static_cast<size_t>(k) < from.ins.size()) return {from.ins[static_cast<size_t>(k)]};
    return {};
  }
  return from.ins;
}

// The other half of a twin pair (-1 = none).
int TwinOf(const Level& level, int node) {
  const int twin = level.nodes[static_cast<size_t>(node)].twin;
  if (twin >= 0) return twin;
  for (size_t n = 0; n < level.nodes.size(); ++n) {
    if (level.nodes[n].twin == node) return static_cast<int>(n);
  }
  return -1;
}

// The ways in a partner comes by: the other half's, or the node's other ins.
std::vector<int> PartnerIns(const Level& level, int node, int lane) {
  const int twin = TwinOf(level, node);
  if (twin >= 0) return level.nodes[static_cast<size_t>(twin)].ins;
  std::vector<int> ins;
  const Node& n = level.nodes[static_cast<size_t>(node)];
  for (size_t k = 0; k < n.ins.size(); ++k) {
    if (static_cast<int>(k) != lane) ins.push_back(n.ins[k]);
  }
  return ins;
}

// Appends a groove's points to a route (the shared joint once).
void Append(std::vector<Vec2>& route, const std::vector<Vec2>& points) {
  route.insert(route.end(), route.empty() ? points.begin() : points.begin() + 1, points.end());
}

}  // namespace

std::vector<Ignite> IgniteFrom(const Level& level, int cup) {
  std::vector<float> reach(level.pipes.size(), -1.0f);  // from the cup to the pipe's downstream end
  for (const int pipe : level.nodes[static_cast<size_t>(cup)].ins) reach[static_cast<size_t>(pipe)] = 0.0f;
  for (bool changed = true; changed;) {
    changed = false;
    for (size_t p = 0; p < level.pipes.size(); ++p) {
      if (reach[p] < 0.0f) continue;
      const float next = reach[p] + GrooveLength(level, static_cast<int>(p));
      for (const int q : Upstream(level, static_cast<int>(p))) {
        float& r = reach[static_cast<size_t>(q)];
        if (r < 0.0f || next < r - 0.01f) {
          r = next;
          changed = true;
        }
      }
    }
  }
  std::vector<Ignite> ignites;
  for (size_t p = 0; p < level.pipes.size(); ++p) {
    if (reach[p] < 0.0f) continue;
    ignites.push_back({static_cast<int>(p), kIgniteStart + reach[p] / kIgniteSpeed,
                       GrooveLength(level, static_cast<int>(p)) / kIgniteSpeed});
  }
  return ignites;
}

Color PartnerColor(const Level& level, int node, int lane) {
  std::vector<int> todo = PartnerIns(level, node, lane), seen;
  Color color = Color::None;
  bool one = true;
  while (!todo.empty()) {
    const int pipe = todo.back();
    todo.pop_back();
    if (std::find(seen.begin(), seen.end(), pipe) != seen.end()) continue;
    seen.push_back(pipe);
    const Node& from = level.nodes[static_cast<size_t>(level.pipes[static_cast<size_t>(pipe)].from)];
    if (from.kind == NodeKind::Spout) {
      for (const Bead& bead : from.beads) {
        for (const Color c : {bead.a, bead.b}) {
          if (c == Color::None) continue;
          if (color == Color::None) color = c;
          one = one && c == color;
        }
      }
    }
    for (const int up : Upstream(level, pipe)) todo.push_back(up);
  }
  return one ? color : Color::None;
}

std::vector<Vec2> PartnerRoute(const Level& level, int node, int lane) {
  // Depth first, up the first way in each time, to the first flask.
  std::vector<int> chain;  // pipes, from the node's partner way in upward
  std::function<bool(int)> climb = [&](int pipe) {
    if (std::find(chain.begin(), chain.end(), pipe) != chain.end()) return false;
    chain.push_back(pipe);
    if (level.nodes[static_cast<size_t>(level.pipes[static_cast<size_t>(pipe)].from)].kind == NodeKind::Spout)
      return true;
    for (const int up : Upstream(level, pipe)) {
      if (climb(up)) return true;
    }
    chain.pop_back();
    return false;
  };
  bool found = false;
  for (const int pipe : PartnerIns(level, node, lane)) {
    if ((found = climb(pipe))) break;
  }
  if (!found) return {};
  std::vector<Vec2> route;
  for (auto it = chain.rbegin(); it != chain.rend(); ++it)
    Append(route, GroovePoints(level, level.pipes[static_cast<size_t>(*it)]));
  if (TwinOf(level, node) >= 0) route.push_back(level.nodes[static_cast<size_t>(node)].at);
  return route;
}

std::vector<Vec2> JamLoop(const Level& level, const std::vector<int>& cycle) {
  if (cycle.size() < 2) return {};
  // The shortest way from a to b down the grooves (pipe indices), or empty.
  auto way = [&](int a, int b) {
    std::vector<float> best(level.nodes.size(), -1.0f);
    std::vector<int> by(level.nodes.size(), -1);
    best[static_cast<size_t>(a)] = 0.0f;
    for (bool changed = true; changed;) {
      changed = false;
      for (size_t p = 0; p < level.pipes.size(); ++p) {
        const Pipe& pipe = level.pipes[p];
        const float from = best[static_cast<size_t>(pipe.from)];
        if (from < 0.0f) continue;
        const float d = from + GrooveLength(level, static_cast<int>(p));
        float& to = best[static_cast<size_t>(pipe.to)];
        if (to < 0.0f || d < to - 0.01f) {
          to = d;
          by[static_cast<size_t>(pipe.to)] = static_cast<int>(p);
          changed = true;
        }
      }
    }
    std::vector<int> pipes;
    for (int n = b; n != a && by[static_cast<size_t>(n)] >= 0 && pipes.size() <= level.pipes.size();
         n = level.pipes[static_cast<size_t>(by[static_cast<size_t>(n)])].from)
      pipes.insert(pipes.begin(), by[static_cast<size_t>(n)]);
    if (pipes.empty() || level.pipes[static_cast<size_t>(pipes.front())].from != a) pipes.clear();
    return pipes;
  };
  std::vector<Vec2> loop;
  for (size_t i = 0; i < cycle.size(); ++i) {
    const int a = cycle[i], b = cycle[(i + 1) % cycle.size()];
    std::vector<int> pipes = way(a, b);
    std::vector<Vec2> leg;
    if (!pipes.empty()) {
      for (const int p : pipes) Append(leg, GroovePoints(level, level.pipes[static_cast<size_t>(p)]));
    } else {
      pipes = way(b, a);  // the circle may run against the flow (two nodes waiting on each other)
      if (pipes.empty()) return {};
      for (const int p : pipes) Append(leg, GroovePoints(level, level.pipes[static_cast<size_t>(p)]));
      std::reverse(leg.begin(), leg.end());
    }
    Append(loop, leg);
  }
  return loop;
}

// ---- The result card ----

namespace {

std::string Capital(std::string text) {
  if (!text.empty() && text[0] >= 'a' && text[0] <= 'z') text[0] = static_cast<char>(text[0] - 'a' + 'A');
  return text;
}

// "a blue", "an amber".
std::string An(const std::string& word) {
  const bool vowel = !word.empty() && std::string("aeiou").find(word[0]) != std::string::npos;
  return (vowel ? "an " : "a ") + word;
}

// "Green 4/6, rose 3/3."
std::string PhialCounts(const Level& level, const Board& board) {
  std::string text;
  for (size_t n = 0; n < level.nodes.size(); ++n) {
    const Node& node = level.nodes[n];
    if (node.kind != NodeKind::Cup) continue;
    text += (text.empty() ? Capital(ColorName(node.want)) : ", " + std::string(ColorName(node.want))) + " " +
            std::to_string(board.Cup(static_cast<int>(n)).count) + "/" + std::to_string(node.count);
  }
  return text.empty() ? "" : text + ".";
}

// The two colours that met in a globe: what its two ways in carry.
std::string SludgeColours(const Level& level, int node) {
  const Node& n = level.nodes[static_cast<size_t>(node)];
  std::vector<Color> colours;
  for (size_t k = 0; n.ins.size() == 2 && k < 2; ++k) {
    const Color c = PartnerColor(level, node, static_cast<int>(1 - k));  // the other lane's partner = this lane
    if (c != Color::None && std::find(colours.begin(), colours.end(), c) == colours.end()) colours.push_back(c);
  }
  if (colours.size() != 2) return "two colours";
  return std::string(ColorName(colours[0])) + " and " + ColorName(colours[1]);
}

}  // namespace

std::string FailureText(const Level& level, const Board& board, const Outcome& outcome) {
  const Node* node = outcome.fail_node >= 0 ? &level.nodes[static_cast<size_t>(outcome.fail_node)] : nullptr;
  const std::string phial = node ? std::string("The ") + ColorName(node->want) + " phial" : "A phial";
  switch (outcome.failure) {
    case Failure::Mud:
      if (node && node->kind == NodeKind::Cup)
        return "The mixer joined colours that don't mix: the sludge ran into the " +
               std::string(ColorName(node->want)) + " phial.";
      return std::string("Nothing held the ") + (node && node->kind == NodeKind::Merge ? "merge" : "crossing") + ": " +
             Capital(SludgeColours(level, outcome.fail_node)) +
             " met in the globe on the same tick. They clotted into sludge.";
    case Failure::WrongCup: return phial + " got another colour.";
    case Failure::Spill: return phial + " got more than it holds.";
    case Failure::Dry: return phial + " ended short.";
    case Failure::Jam: {
      const int lonely = board.Jam().lonely;
      std::string why = "Each mixer holds one drop and waits for the other's: neither can finish.";
      if (lonely >= 0) {
        Color held = Color::None;
        int lane = 0;
        for (const Drop& drop : board.Drops()) {
          if (drop.alive && drop.loc.kind == Loc::InNode && drop.loc.index == lonely) {
            held = drop.bead.a;
            lane = std::max(0, drop.lane_in);
          }
        }
        const Color partner = PartnerColor(level, lonely, lane);
        const std::string x = held == Color::None ? "drop" : ColorName(held);
        const std::string p = partner == Color::None ? "a partner" : ColorName(partner);
        const int piece = board.PieceAt(lonely);
        const bool twin = piece >= 0 && board.PieceInfo(piece).piece.type == PieceType::TwinGate;
        if (twin) {
          why = partner == Color::None
                    ? "The last " + x + " waits at its seal for a twin, but none is coming. The seals never open again."
                    : "The last " + x + " waits at its seal for " + An(p) + " twin, but the " + p +
                          " flask is empty. The seals never open again.";
        } else {
          why = "The mixer holds " + An(x) + " and waits for " + p + ", but none is coming. Its seal cracks.";
        }
      }
      return why + " " + PhialCounts(level, board);
    }
    case Failure::None: break;
  }
  return "";
}

}  // namespace threadlock::sigil
