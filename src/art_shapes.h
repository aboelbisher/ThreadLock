#pragma once

#include <memory>
#include <vector>

#include <glm/glm.hpp>

#include "palette.h"
#include "polyline.h"
#include "wlw/rendering/texture.h"
#include "wlw/ui/canvas.h"

// Drawing helpers every board look shares: smooth paths and bands along them, ellipses, the colour
// glyphs, soft glows, and the brass of the toolkit (DESIGN.md §5: one brass toolkit in every world).
namespace threadlock::art {

using wlw::ui::Canvas;
using wlw::ui::Rect;

inline constexpr float kPi = 3.14159265358979f;

// A band along `path` between two offsets from its centre line (negative = left of the way it
// runs), made of triangles that don't overlap: right for translucent fills (glass), whose overlaps
// would show. Strong colours are better drawn as Stroke (thin triangles fade at their shared edges).
void Ribbon(Canvas& canvas, const Path& path, float from, float to, const Rgba& color);
// A line along `path` offset from its centre (opaque colours: its round joints overlap).
void Stroke(Canvas& canvas, const Path& path, float offset, float thickness, const Rgba& color);
void Polyline(Canvas& canvas, const std::vector<glm::vec2>& points, float thickness, const Rgba& color);
void DashedLine(Canvas& canvas, glm::vec2 a, glm::vec2 b, float thickness, const Rgba& color, int dashes);

// An ellipse (axis-aligned radii) between two angles (0 = right, clockwise as y points down).
void EllipseArc(Canvas& canvas, glm::vec2 centre, glm::vec2 radii, float thickness, const Rgba& color,
                float from = 0.0f, float to = 2.0f * kPi);
void FilledEllipse(Canvas& canvas, glm::vec2 centre, glm::vec2 radii, const Rgba& color);
// A convex polygon, filled (a fan of triangles).
void FillConvex(Canvas& canvas, const std::vector<glm::vec2>& points, const Rgba& color);

// The shape that backs up each colour for colour-blind players (DESIGN.md §3).
void Glyph(Canvas& canvas, Color color, glm::vec2 centre, float r, const Rgba& fill);

// A soft round glow (a (1 - d²)² dot), drawn additively: halos round lanterns, light on glass.
class Glow {
 public:
  void Draw(Canvas& canvas, glm::vec2 centre, glm::vec2 radii, const Rgba& color) const;
  void Draw(Canvas& canvas, glm::vec2 centre, float radius, const Rgba& color) const {
    Draw(canvas, centre, {radius, radius}, color);
  }

 private:
  mutable std::shared_ptr<wlw::rendering::WTexture2D> texture_;
};

// ---- Brass (the toolkit) ----
namespace brass {
inline constexpr Rgba kLight = Hex(0xFCE6B0);
inline constexpr Rgba kMid = Hex(0xC9974E);
inline constexpr Rgba kShade = Hex(0x8C6326);
inline constexpr Rgba kDark = Hex(0x553812);
inline constexpr Rgba kRivet = Hex(0x4A3210);
inline constexpr Rgba kEnamel = Hex(0x0B1620);
inline constexpr Rgba kEnamelLight = Hex(0x1F3346);
inline constexpr Rgba kIndicator = Hex(0xE6F4FF);  // the white marks on a piece
}  // namespace brass

// A brass ring: a lit top-left, a shaded bottom-right (a turned metal edge).
void BrassRing(Canvas& canvas, glm::vec2 centre, float radius, float thickness, float alpha = 1.0f);
// A medallion: a soft shadow, a brass rim, an enamel face and four rivets. Returns the face's radius.
float Medallion(Canvas& canvas, glm::vec2 centre, float radius, float alpha = 1.0f, bool rivets = true);
// A brass plate with rounded corners and an enamel face inset by `rim`.
void Plate(Canvas& canvas, const Rect& rect, float corner, float rim, float alpha = 1.0f);
// A flat brass band (a collar, a cap), shaded from light at the top to dark at the bottom.
void BrassBar(Canvas& canvas, glm::vec2 centre, glm::vec2 half, float angle, float alpha = 1.0f);

}  // namespace threadlock::art
