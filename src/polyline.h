#pragma once

#include <vector>

#include <glm/glm.hpp>

// Polylines in board or canvas units (plain C++: the rules' canals and the art share them).
namespace threadlock::art {

// A polyline with the distance from its start to each point.
struct Path {
  std::vector<glm::vec2> points;
  std::vector<float> at;

  float Length() const { return at.empty() ? 0.0f : at.back(); }
  glm::vec2 PointAt(float distance) const;
  glm::vec2 DirectionAt(float distance) const;  // unit
  // The part between two distances from the start.
  Path Part(float from, float to) const;
  bool Empty() const { return points.size() < 2; }
};
Path MakePath(std::vector<glm::vec2> points);

// The same corners, rounded: each bend becomes an arc of `radius` (smaller where the segments next to
// it are short), sampled every `step` along the arc. Straight runs keep their two end points.
std::vector<glm::vec2> RoundCorners(const std::vector<glm::vec2>& points, float radius, float step);

}  // namespace threadlock::art
