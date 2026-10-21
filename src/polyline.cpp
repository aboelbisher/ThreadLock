#include "polyline.h"

#include <algorithm>
#include <cmath>

namespace threadlock::art {
namespace {

glm::vec2 Unit(glm::vec2 v) {
  const float length = glm::length(v);
  return length > 1e-5f ? v / length : glm::vec2(0.0f, 1.0f);
}

}  // namespace

Path MakePath(std::vector<glm::vec2> points) {
  Path path;
  path.points = std::move(points);
  path.at.reserve(path.points.size());
  float total = 0.0f;
  for (size_t i = 0; i < path.points.size(); ++i) {
    if (i > 0) total += glm::distance(path.points[i - 1], path.points[i]);
    path.at.push_back(total);
  }
  return path;
}

glm::vec2 Path::PointAt(float distance) const {
  if (points.empty()) return {};
  if (distance <= 0.0f || points.size() == 1) return points.front();
  const auto it = std::lower_bound(at.begin(), at.end(), distance);
  if (it == at.end()) return points.back();
  const size_t i = static_cast<size_t>(it - at.begin());
  if (i == 0) return points.front();
  const float span = at[i] - at[i - 1];
  return span > 1e-6f ? glm::mix(points[i - 1], points[i], (distance - at[i - 1]) / span) : points[i];
}

glm::vec2 Path::DirectionAt(float distance) const {
  if (points.size() < 2) return {0.0f, 1.0f};
  const auto it = std::lower_bound(at.begin(), at.end(), std::max(distance, 1e-4f));
  size_t i = it == at.end() ? points.size() - 1 : static_cast<size_t>(it - at.begin());
  i = std::clamp<size_t>(i, 1, points.size() - 1);
  // Skip repeated points.
  while (i + 1 < points.size() && glm::distance(points[i - 1], points[i]) < 1e-5f) ++i;
  return Unit(points[i] - points[i - 1]);
}

Path Path::Part(float from, float to) const {
  from = std::clamp(from, 0.0f, Length());
  to = std::clamp(to, from, Length());
  std::vector<glm::vec2> part{PointAt(from)};
  for (size_t i = 0; i < points.size(); ++i) {
    if (at[i] > from && at[i] < to) part.push_back(points[i]);
  }
  part.push_back(PointAt(to));
  return MakePath(std::move(part));
}

std::vector<glm::vec2> RoundCorners(const std::vector<glm::vec2>& points, float radius, float step) {
  if (points.size() < 3) return points;
  std::vector<glm::vec2> out{points.front()};
  for (size_t i = 1; i + 1 < points.size(); ++i) {
    const glm::vec2 p = points[i];
    const glm::vec2 a = points[i - 1], b = points[i + 1];
    const glm::vec2 d_in = Unit(p - a), d_out = Unit(b - p);
    const float cos_turn = std::clamp(glm::dot(d_in, d_out), -1.0f, 1.0f);
    const float turn = std::acos(cos_turn);  // 0 = straight on
    if (turn < 0.02f) {
      out.push_back(p);
      continue;
    }
    // The arc touches both segments `cut` away from the corner; it may use at most half of each
    // (the next corner needs the other half).
    const float half_tan = std::tan(turn * 0.5f);
    float cut = radius * half_tan;
    cut = std::min({cut, glm::distance(a, p) * 0.5f, glm::distance(p, b) * 0.5f});
    const float r = cut / std::max(half_tan, 1e-4f);
    const glm::vec2 start = p - d_in * cut;
    // The centre is on the inside of the bend.
    const float side = (d_in.x * d_out.y - d_in.y * d_out.x) > 0.0f ? 1.0f : -1.0f;  // + = turning clockwise
    const glm::vec2 inward = side > 0.0f ? glm::vec2(-d_in.y, d_in.x) : glm::vec2(d_in.y, -d_in.x);
    const glm::vec2 centre = start + inward * r;
    const glm::vec2 from = start - centre;
    const int steps = std::max(2, static_cast<int>(std::ceil(r * turn / std::max(step, 0.5f))));
    for (int k = 0; k <= steps; ++k) {
      const float t = side * turn * static_cast<float>(k) / static_cast<float>(steps);
      const float c = std::cos(t), s = std::sin(t);
      out.push_back(centre + glm::vec2(from.x * c - from.y * s, from.x * s + from.y * c));
    }
  }
  out.push_back(points.back());
  return out;
}

}  // namespace threadlock::art
