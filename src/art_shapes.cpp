#include "art_shapes.h"

#include <algorithm>
#include <cmath>

#include "wlw/utils/textures.h"

namespace threadlock::art {
namespace {

glm::vec2 Unit(glm::vec2 v) {
  const float length = glm::length(v);
  return length > 1e-5f ? v / length : glm::vec2(0.0f, 1.0f);
}

glm::vec2 Left(glm::vec2 d) { return {d.y, -d.x}; }  // y points down: (d.y, -d.x) is to the left of d

}  // namespace

// ---- Paths ----

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

// ---- Strokes and bands ----

void Ribbon(Canvas& canvas, const Path& path, float from, float to, const Rgba& color) {
  const size_t n = path.points.size();
  if (n < 2) return;
  // A side vector per point: the mean of the neighbouring segments' normals, lengthened at bends so
  // the band keeps its width (a miter, limited).
  std::vector<glm::vec2> side(n);
  for (size_t i = 0; i < n; ++i) {
    const glm::vec2 d_prev = i > 0 ? Unit(path.points[i] - path.points[i - 1]) : Unit(path.points[1] - path.points[0]);
    const glm::vec2 d_next = i + 1 < n ? Unit(path.points[i + 1] - path.points[i]) : d_prev;
    const glm::vec2 l = Left(Unit(d_prev + d_next));
    const float miter = std::max(0.5f, glm::dot(l, Left(d_prev)));
    side[i] = l / miter;
  }
  for (size_t i = 0; i + 1 < n; ++i) {
    if (glm::distance(path.points[i], path.points[i + 1]) < 1e-4f) continue;
    const glm::vec2 a0 = path.points[i] - side[i] * from, b0 = path.points[i] - side[i] * to;
    const glm::vec2 a1 = path.points[i + 1] - side[i + 1] * from, b1 = path.points[i + 1] - side[i + 1] * to;
    canvas.Triangle(a0, b0, b1, color);
    canvas.Triangle(a0, b1, a1, color);
  }
}

void Stroke(Canvas& canvas, const Path& path, float offset, float thickness, const Rgba& color) {
  const size_t n = path.points.size();
  if (n < 2) return;
  std::vector<glm::vec2> points(n);
  for (size_t i = 0; i < n; ++i) {
    const glm::vec2 d_prev = i > 0 ? Unit(path.points[i] - path.points[i - 1]) : Unit(path.points[1] - path.points[0]);
    const glm::vec2 d_next = i + 1 < n ? Unit(path.points[i + 1] - path.points[i]) : d_prev;
    const glm::vec2 l = Left(Unit(d_prev + d_next));
    points[i] = path.points[i] - l * (offset / std::max(0.5f, glm::dot(l, Left(d_prev))));
  }
  for (size_t i = 1; i < n; ++i) canvas.Line(points[i - 1], points[i], thickness, color);
}

void Polyline(Canvas& canvas, const std::vector<glm::vec2>& points, float thickness, const Rgba& color) {
  for (size_t i = 1; i < points.size(); ++i) canvas.Line(points[i - 1], points[i], thickness, color);
}

void DashedLine(Canvas& canvas, glm::vec2 a, glm::vec2 b, float thickness, const Rgba& color, int dashes) {
  for (int i = 0; i < dashes; ++i) {
    const float t0 = (static_cast<float>(i) + 0.15f) / static_cast<float>(dashes);
    const float t1 = (static_cast<float>(i) + 0.7f) / static_cast<float>(dashes);
    canvas.Line(a + (b - a) * t0, a + (b - a) * t1, thickness, color);
  }
}

void EllipseArc(Canvas& canvas, glm::vec2 centre, glm::vec2 radii, float thickness, const Rgba& color, float from,
                float to) {
  const float sweep = to - from;
  const int steps = std::max(4, static_cast<int>(std::ceil(std::fabs(sweep) * std::max(radii.x, radii.y) / 3.0f)));
  glm::vec2 previous = centre + radii * glm::vec2(std::cos(from), std::sin(from));
  for (int k = 1; k <= steps; ++k) {
    const float a = from + sweep * static_cast<float>(k) / static_cast<float>(steps);
    const glm::vec2 p = centre + radii * glm::vec2(std::cos(a), std::sin(a));
    canvas.Line(previous, p, thickness, color);
    previous = p;
  }
}

void FilledEllipse(Canvas& canvas, glm::vec2 centre, glm::vec2 radii, const Rgba& color) {
  constexpr int kSteps = 28;
  std::vector<glm::vec2> points;
  points.reserve(kSteps);
  for (int k = 0; k < kSteps; ++k) {
    const float a = 2.0f * kPi * static_cast<float>(k) / static_cast<float>(kSteps);
    points.push_back(centre + radii * glm::vec2(std::cos(a), std::sin(a)));
  }
  FillConvex(canvas, points, color);
}

void FillConvex(Canvas& canvas, const std::vector<glm::vec2>& points, const Rgba& color) {
  if (points.size() < 3) return;
  glm::vec2 centre{0.0f};
  for (const glm::vec2& p : points) centre += p;
  centre /= static_cast<float>(points.size());
  // Each triangle fades out over its edges, the inner ones too: a solid colour drawn once shows faint
  // seams there, so it's drawn twice (the seams' coverage goes from 3/4 to 15/16; outer edges stay smooth).
  const int passes = color.a >= 0.99f ? 2 : 1;
  for (int pass = 0; pass < passes; ++pass) {
    for (size_t i = 0; i < points.size(); ++i)
      canvas.Triangle(centre, points[i], points[(i + 1) % points.size()], color);
  }
}

void Glyph(Canvas& canvas, Color color, glm::vec2 c, float r, const Rgba& fill) {
  switch (color) {
    case Color::Blue: canvas.Circle(c, r, fill); break;
    case Color::Amber:
      canvas.Triangle(c + glm::vec2(0.0f, -r * 1.1f), c + glm::vec2(r, r * 0.8f), c + glm::vec2(-r, r * 0.8f), fill);
      break;
    case Color::Green:
      FillConvex(canvas,
                 {c + glm::vec2(0.0f, -r * 1.15f), c + glm::vec2(r * 1.15f, 0.0f), c + glm::vec2(0.0f, r * 1.15f),
                  c + glm::vec2(-r * 1.15f, 0.0f)},
                 fill);
      break;
    case Color::Rose: canvas.FillRect({c - glm::vec2(r * 0.85f), c + glm::vec2(r * 0.85f)}, fill, r * 0.15f); break;
    case Color::Pearl:
      canvas.Triangle(c + glm::vec2(0.0f, -r * 1.3f), c + glm::vec2(r * 0.35f, 0.0f), c + glm::vec2(-r * 0.35f, 0.0f),
                      fill);
      canvas.Triangle(c + glm::vec2(0.0f, r * 1.3f), c + glm::vec2(-r * 0.35f, 0.0f), c + glm::vec2(r * 0.35f, 0.0f),
                      fill);
      canvas.Triangle(c + glm::vec2(-r * 1.3f, 0.0f), c + glm::vec2(0.0f, -r * 0.35f), c + glm::vec2(0.0f, r * 0.35f),
                      fill);
      canvas.Triangle(c + glm::vec2(r * 1.3f, 0.0f), c + glm::vec2(0.0f, r * 0.35f), c + glm::vec2(0.0f, -r * 0.35f),
                      fill);
      break;
    default: break;
  }
}

void Glow::Draw(Canvas& canvas, glm::vec2 centre, glm::vec2 radii, const Rgba& color) const {
  if (!texture_) texture_ = wlw::utils::RadialFalloffTexture(64);
  wlw::ui::ImageStyle style;
  style.tint = color;
  style.blend = wlw::rendering::BlendMode::Additive;
  canvas.Image({centre - radii, centre + radii}, texture_, style);
}

// ---- Brass ----

void BrassRing(Canvas& canvas, glm::vec2 c, float r, float thickness, float alpha) {
  // Light falls from the top left (Arc angles: 0 = up, clockwise): a lit arc there, a dark one opposite.
  canvas.Ring(c, r, thickness, WithAlpha(brass::kShade, alpha));
  canvas.Arc(c, r, thickness * 0.8f, -kPi * 0.85f, kPi * 1.2f, WithAlpha(brass::kMid, alpha));
  canvas.Arc(c, r - thickness * 0.12f, thickness * 0.38f, -kPi * 0.6f, kPi * 0.7f, WithAlpha(brass::kLight, alpha));
  canvas.Arc(c, r + thickness * 0.15f, thickness * 0.35f, kPi * 0.42f, kPi * 0.66f, WithAlpha(brass::kDark, alpha));
}

float Medallion(Canvas& canvas, glm::vec2 c, float r, float alpha, bool rivets) {
  canvas.Circle(c + glm::vec2(0.0f, r * 0.22f), r * 1.06f, Rgba{0.0f, 0.0f, 0.0f, 0.38f * alpha});
  canvas.Circle(c, r, WithAlpha(brass::kShade, alpha));
  const float face = r * 0.76f;
  BrassRing(canvas, c, (r + face) * 0.5f, r - face, alpha);
  canvas.Circle(c, face, WithAlpha(brass::kEnamel, alpha));
  canvas.Circle(c - glm::vec2(face * 0.16f, face * 0.2f), face * 0.66f, WithAlpha(brass::kEnamelLight, 0.55f * alpha));
  canvas.Ring(c, face, std::max(0.6f, r * 0.05f), WithAlpha(brass::kDark, 0.9f * alpha));
  if (rivets) {
    for (int k = 0; k < 4; ++k) {
      const float a = kPi * (0.25f + 0.5f * static_cast<float>(k));
      canvas.Circle(c + glm::vec2(std::cos(a), std::sin(a)) * ((r + face) * 0.5f), r * 0.065f,
                    WithAlpha(brass::kRivet, alpha));
    }
  }
  return face;
}

void Plate(Canvas& canvas, const Rect& rect, float corner, float rim, float alpha) {
  const glm::vec2 shadow{0.0f, rim * 0.9f};
  canvas.FillRect({rect.min + shadow, rect.max + shadow}, Rgba{0.0f, 0.0f, 0.0f, 0.38f * alpha}, corner);
  canvas.FillRect(rect, WithAlpha(brass::kShade, alpha), corner);
  canvas.FillRect({rect.min + glm::vec2(rim * 0.15f), rect.max - glm::vec2(rim * 0.3f)}, WithAlpha(brass::kMid, alpha),
                  corner);
  if (rim <= 0.0f) {  // solid brass: a cap, a band
    const float h = rect.Size().y;
    canvas.FillRect({rect.min + glm::vec2(h * 0.25f, h * 0.15f), {rect.max.x - h * 0.4f, rect.min.y + h * 0.4f}},
                    WithAlpha(brass::kLight, 0.75f * alpha), h * 0.12f);
    return;
  }
  canvas.FillRect({rect.min + glm::vec2(rim * 0.3f), {rect.max.x - rim * 0.6f, rect.min.y + rim * 0.55f}},
                  WithAlpha(brass::kLight, 0.7f * alpha), rim * 0.25f);
  const Rect face{rect.min + glm::vec2(rim), rect.max - glm::vec2(rim)};
  canvas.FillRect(face, WithAlpha(brass::kEnamel, alpha), std::max(0.0f, corner - rim));
  canvas.FillRect({face.min + glm::vec2(1.0f), {face.max.x - 2.0f, face.Center().y}},
                  WithAlpha(brass::kEnamelLight, 0.35f * alpha), std::max(0.0f, corner - rim - 1.0f));
}

void BrassBar(Canvas& canvas, glm::vec2 c, glm::vec2 half, float angle, float alpha) {
  // A capsule along `angle` (half.x long each way, half.y thick each way).
  const glm::vec2 along{std::cos(angle), std::sin(angle)};
  const glm::vec2 across{-along.y, along.x};
  const glm::vec2 a = c - along * std::max(0.0f, half.x - half.y), b = c + along * std::max(0.0f, half.x - half.y);
  // Which side faces up decides where the light is.
  const glm::vec2 up = across.y < 0.0f ? across : -across;
  canvas.Line(a - up * (half.y * 0.15f), b - up * (half.y * 0.15f), half.y * 2.1f, WithAlpha(brass::kDark, alpha));
  canvas.Line(a, b, half.y * 1.8f, WithAlpha(brass::kMid, alpha));
  canvas.Line(a + up * (half.y * 0.45f), b + up * (half.y * 0.45f), half.y * 0.6f, WithAlpha(brass::kLight, alpha));
}

}  // namespace threadlock::art
