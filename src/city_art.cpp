#include "city_art.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "ui_parts.h"
#include "wlw/rendering/shader.h"
#include "wlw/utils/textures.h"

namespace threadlock {
namespace {

using art::kPi;
using city::kWidth;
using wlw::ui::Align;
using wlw::ui::Canvas;
using wlw::ui::Rect;

constexpr float kLow = 1400.0f;  // everything under the tower stands this much lower than in the first mock
constexpr int kTilePool = 3;
constexpr float kText = 1.15f;  // the mock's CSS font sizes -> canvas line heights

// The city at night.
constexpr Rgba kWall = Hex(0x0C121E);
constexpr Rgba kTowerFace = Hex(0x131B2A);
constexpr Rgba kCourse = Hex(0x1A2335);
constexpr Rgba kRoof = Hex(0x1A2131);
constexpr Rgba kRoofEdge = Hex(0x2E3A52);
constexpr Rgba kSlate = Hex(0x232C40);
constexpr Rgba kStone = Hex(0x1A2233);
constexpr Rgba kGlassDark = Hex(0x0A0F18);
constexpr Rgba kPane = Hex(0x121C2C);
constexpr Rgba kSmallPane = Hex(0x101827);
constexpr Rgba kLobe = Hex(0x111B2B);
// Brass and light.
constexpr Rgba kBrass = Hex(0xC9974E);
constexpr Rgba kBrassDim = Hex(0x6E5A3A);
constexpr Rgba kBrassLit = Hex(0xF6DA9A);
constexpr Rgba kGold = Hex(0xF0C060);
constexpr Rgba kLeadLit = Hex(0x8A6A3A);
constexpr Rgba kLeadDark = Hex(0x2A3550);
constexpr Rgba kLeadLocked = Hex(0x070B12);
constexpr Rgba kLight = Hex(0xFFD47A);
constexpr Rgba kWarm = Hex(0xFFD58A);
constexpr Rgba kBead = Hex(0xFFF4D6);
constexpr Rgba kLeanLight = Hex(0xFFD27A);
constexpr Rgba kSwiftLight = Hex(0x8FE3FF);
constexpr Rgba kPipeIron = Hex(0x1C2638);
constexpr Rgba kPipeShine = Hex(0x33415C);

uint64_t Mix(uint64_t h, uint64_t v) {
  h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
  return h;
}

uint64_t Bits(float f) {
  uint32_t u = 0;
  std::memcpy(&u, &f, sizeof(u));
  return u;
}

// The mock's random numbers (Park-Miller), so its houses stand where they stood.
class Rng {
 public:
  explicit Rng(int64_t seed) : s_(seed) {}
  float operator()() {
    s_ = s_ * 16807 % 2147483647;
    return static_cast<float>(static_cast<double>(s_) / 2147483647.0);
  }

 private:
  int64_t s_;
};

float Clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }
float Smooth(float t) { return t * t * (3.0f - 2.0f * t); }

void Outline(Canvas& c, const std::vector<glm::vec2>& points, float width, const Rgba& color) {
  for (size_t i = 0; i < points.size(); ++i) c.Line(points[i], points[(i + 1) % points.size()], width, color);
}

void RoundRect(Canvas& c, const Rect& r, float corner, const Rgba& fill, const Rgba& stroke, float width) {
  c.FillRect(r, stroke, corner);
  c.FillRect({r.min + glm::vec2(width), r.max - glm::vec2(width)}, fill, std::max(0.0f, corner - width));
}

// A window or a door: a rectangle under a round arch (`yc` = the arch's centre line), map units.
template <typename At>
void Arched(Canvas& c, const At& at, float s, float x0, float x1, float yc, float yb, const Rgba& fill,
            const Rgba& stroke, float width) {
  const float r = (x1 - x0) * 0.5f;
  const glm::vec2 centre = at((x0 + x1) * 0.5f, yc);
  c.FillRect({at(x0, yc), at(x1, yb)}, fill);
  c.Circle(centre, r * s, fill);
  if (width <= 0.0f) return;
  c.Line(at(x0, yb), at(x0, yc), width * s, stroke);
  c.Line(at(x1, yc), at(x1, yb), width * s, stroke);
  c.Line(at(x0, yb), at(x1, yb), width * s, stroke);
  c.Arc(centre, r * s, width * s, -kPi * 0.5f, kPi, stroke);
}

// Opaque fills go down twice: the anti-aliased edges of the triangles inside a shape leave faint seams,
// which a second pass covers (a translucent fill would darken, so it goes once).
int Passes(const Rgba& color) { return color.a >= 0.99f ? 2 : 1; }

void FillShape(Canvas& c, const std::vector<glm::vec2>& points, const Rgba& color) {
  for (int i = Passes(color); i > 0; --i) art::FillConvex(c, points, color);
}

void FillBand(Canvas& c, const std::vector<glm::vec2>& inner, const std::vector<glm::vec2>& outer, const Rgba& color) {
  for (int pass = Passes(color); pass > 0; --pass) {
    for (size_t k = 0; k + 1 < inner.size(); ++k) {
      c.Triangle(inner[k], outer[k], outer[k + 1], color);
      c.Triangle(inner[k], outer[k + 1], inner[k + 1], color);
    }
  }
}

// A band's outline: along the outside, back along the inside.
std::vector<glm::vec2> BandOutline(const std::vector<glm::vec2>& inner, const std::vector<glm::vec2>& outer) {
  std::vector<glm::vec2> points(outer.begin(), outer.end());
  points.insert(points.end(), inner.rbegin(), inner.rend());
  return points;
}

// Dots along a polyline every `spacing`, starting `phase` in (canvas units).
void Dots(Canvas& c, const std::vector<glm::vec2>& points, float spacing, float phase, float r, const Rgba& color) {
  float next = std::fmod(phase, spacing);
  if (next < 0.0f) next += spacing;
  float walked = 0.0f;
  for (size_t i = 0; i + 1 < points.size(); ++i) {
    const glm::vec2 a = points[i], b = points[i + 1];
    const float length = glm::distance(a, b);
    while (next <= walked + length) {
      c.Circle(a + (b - a) * ((next - walked) / std::max(length, 1e-4f)), r, color);
      next += spacing;
    }
    walked += length;
  }
}

// A convex polygon cut to the slab x0 <= x <= x1 (Sutherland-Hodgman, two planes).
std::vector<glm::vec2> ClipSlab(const std::vector<glm::vec2>& polygon, float x0, float x1) {
  auto clip = [](const std::vector<glm::vec2>& in, float edge, bool keep_above) {
    std::vector<glm::vec2> out;
    for (size_t i = 0; i < in.size(); ++i) {
      const glm::vec2 a = in[i], b = in[(i + 1) % in.size()];
      const bool a_in = keep_above ? a.x >= edge : a.x <= edge;
      const bool b_in = keep_above ? b.x >= edge : b.x <= edge;
      if (a_in) out.push_back(a);
      if (a_in != b_in) out.push_back(a + (b - a) * ((edge - a.x) / (b.x - a.x)));
    }
    return out;
  };
  return clip(clip(polygon, x0, true), x1, false);
}

std::vector<glm::vec2> CirclePoints(glm::vec2 c, float r, int n = 28) {
  std::vector<glm::vec2> points;
  for (int i = 0; i < n; ++i) {
    const float a = 2.0f * kPi * static_cast<float>(i) / static_cast<float>(n);
    points.push_back(c + glm::vec2(std::cos(a), std::sin(a)) * r);
  }
  return points;
}

std::vector<glm::vec2> DiamondPoints(glm::vec2 c, float r) {
  return {c + glm::vec2(0.0f, -r), c + glm::vec2(r, 0.0f), c + glm::vec2(0.0f, r), c + glm::vec2(-r, 0.0f)};
}

// The plate's "2 · Fair Shares": the number, a dot, the name, centred on `at`.
void Title(Canvas& c, const wlw::ui::Font& font, const std::string& number, const std::string& name, glm::vec2 at,
           float size, const Rgba& color) {
  const float gap = size * 0.55f;
  const float wn = font.Measure(number, size), wt = font.Measure(name, size);
  const float left = at.x - (wn + gap + wt) * 0.5f;
  c.Text(font, number, {left, at.y}, size, Align::Left, {.color = color});
  c.Circle({left + wn + gap * 0.5f, at.y}, size * 0.09f, color);
  c.Text(font, name, {left + wn + gap, at.y}, size, Align::Left, {.color = color});
}

}  // namespace

void CityArt::Create(wlw::app::Application& app) {
  using namespace wlw;
  tiles_.Create(app.Window(), "City map", Hex(0x080C15));
  const auto gradient = rendering::Shader::FromFile("shaders/ui_gradient.shader");
  sky_top_ = rendering::ShaderMaterial::Create(gradient);
  sky_low_ = rendering::ShaderMaterial::Create(gradient);
  soft_ = utils::RadialFalloffTexture(64);
}

void CityArt::Release() { tiles_.Shrink(0); }

void CityArt::Build() {
  built_ = true;
  // Rooftops round the tower's foot, then the market square's and the canal quarter's houses, in the
  // order the mock drew them (they share one sequence of random numbers).
  Rng r(11);
  auto houses = [&](float yb, float x0, float x1, float w_min, float w_span, float h_min, float h_span, Rgba fill,
                    Rgba stroke, float width) {
    float x = x0;
    while (x < x1) {
      const float w = w_min + r() * w_span;
      const float h = h_min + r() * h_span;
      const float g = 12.0f + r() * 16.0f;
      Shape shape;
      shape.points = {{x, yb}, {x, yb - h}, {x + w / 2.0f, yb - h - g}, {x + w, yb - h}, {x + w, yb}};
      shape.fill = fill;
      shape.stroke = stroke;
      shape.width = width;
      shape.top = yb - h - g;
      shape.bottom = yb;
      shapes_.push_back(std::move(shape));
      x += w + 2.0f + r() * 6.0f;
    }
  };
  houses(930 + kLow, -12, 140, 34, 22, 40, 50, Hex(0x0F1624), Hex(0x26324A), 1.5f);
  houses(930 + kLow, 252, 400, 34, 22, 40, 50, Hex(0x0F1624), Hex(0x26324A), 1.5f);
  for (float yb : {1420.0f, 1640.0f, 1860.0f, 2060.0f})
    houses(yb + kLow, -10, 400, 38, 26, 34, 40, Hex(0x0D1524), Hex(0x22314A), 1.2f);
  for (float yb : {2398.0f, 2728.0f, 3000.0f})
    houses(yb + kLow, -10, 400, 24, 12, 50, 50, Hex(0x0D1422), Hex(0x22314A), 1.2f);

  Rng stars(5);
  for (int k = 0; k < 30; ++k) {
    Dot dot;
    dot.at.x = stars() * 390.0f;
    dot.at.y = 62.0f + stars() * 200.0f;
    dot.r = 0.6f + stars() * 1.2f;
    dot.alpha = 0.4f + stars() * 0.6f;
    sky_stars_.push_back(dot);
  }

  for (int w = 2; w <= city::kDistricts; ++w) {
    // The dotted path through a district's first 15 lamps: the same smooth curve as the lantern path.
    const auto lamps = city::DistrictLamps(w);
    std::vector<Vec2>& path = lamp_paths_[static_cast<size_t>(w - 1)];
    for (int j = 0; j + 1 < 15; ++j) {
      const Vec2 p0 = lamps[std::max(0, j - 1)], p1 = lamps[j], p2 = lamps[j + 1], p3 = lamps[std::min(14, j + 2)];
      const city::Cubic piece{p1,
                              {p1.x + (p2.x - p0.x) / 6.0f, p1.y + (p2.y - p0.y) / 6.0f},
                              {p2.x - (p3.x - p1.x) / 6.0f, p2.y - (p3.y - p1.y) / 6.0f},
                              p2};
      std::vector<Vec2> points = piece.Points(10);
      path.insert(path.end(), points.begin() + (j == 0 ? 0 : 1), points.end());
    }
  }
  for (size_t p = 0; p < city::Trail().size(); ++p) trail_.push_back(city::TrailPoints(static_cast<int>(p), 16));
}

// ---- The still picture ----

void CityArt::Draw(Canvas& picture, Canvas& canvas, const CityFrame& frame, float heading) {
  if (!built_) Build();
  const glm::vec2 screen = frame.screen;
  const float s = frame.scale;
  const float tile_h = screen.y / s;  // map units
  const TilePool::Span span = TilePool::Visible(frame.scroll, tile_h, city::kHeight);
  while (tiles_.Count() < kTilePool) tiles_.Add();

  uint64_t screen_key = Mix(1, Bits(screen.x));
  screen_key = Mix(screen_key, Bits(screen.y));
  screen_key = Mix(screen_key, Bits(s));
  screen_key = Mix(screen_key, Bits(picture.PixelsPerUnit()));
  auto want = [&](int k) {
    uint64_t key = Mix(screen_key, static_cast<uint64_t>(k) + 1u);
    if (static_cast<float>(k) * tile_h < 300.0f) key = Mix(key, static_cast<uint64_t>(std::min(frame.stars, 45)) + 7u);
    return key;
  };
  // Tiles on screen draw as soon as they can; then one spare tile ahead of the view.
  bool drew = false;
  for (int k = span.first; k <= span.last; ++k) {
    const int slot = tiles_.Hold(k, span.first, span.last);
    if (slot < 0 || tiles_[slot].fresh || tiles_[slot].base == want(k)) continue;
    DrawStill(slot, k, frame, tile_h);
    tiles_[slot].base = tiles_[slot].key = want(k);
    drew = true;
  }
  const int ahead = heading > 0.0f ? span.last + 1 : heading < 0.0f ? span.first - 1 : -1;
  if (!drew && ahead >= 0 && ahead < span.count) {
    const int slot = tiles_.Hold(ahead, span.first, span.last);
    if (slot >= 0 && !tiles_[slot].fresh && tiles_[slot].base != want(ahead)) {
      DrawStill(slot, ahead, frame, tile_h);
      tiles_[slot].base = tiles_[slot].key = want(ahead);
    }
  }
  tiles_.Draw(picture, span.first, span.last, frame.scroll * s);
  tiles_.EndFrame();

  const Xf x{s, frame.scroll};
  DrawLights(picture, x, frame);
  DrawMist(picture, x, frame);
  DrawDistricts(picture, canvas, x, frame);
  DrawWindow(picture, x, frame);
  DrawLanterns(picture, canvas, x, frame);
  if (frame.scroll < 290.0f)
    Title(canvas, *frame.font, frame.tower, "THE LANTERN WORKS", x(195, 273), 10.5f * kText * s, kBrass);
}

void CityArt::DrawStill(int slot, int k, const CityFrame& frame, float tile_h) {
  TilePool::Slot& tile = tiles_[slot];
  Canvas& c = tile.view->GetCanvas();
  tile.view->SetUpdateMode(wlw::scene::Viewport::UpdateMode::Once);
  const float top = static_cast<float>(k) * tile_h, bottom = top + tile_h;
  const Xf x{frame.scale, top};
  if (top < 300.0f) DrawSky(c, x, frame);
  DrawTower(c, x, top, bottom);
  DrawTown(c, x, top, bottom);
}

void CityArt::DrawSky(Canvas& c, const Xf& x, const CityFrame& frame) {
  const float dawn = city::Dawn(frame.stars);
  const Rgba top = Mixed(Hex(0x050814), Hex(0x2A4A86), dawn);
  const Rgba mid = Mixed(Hex(0x121A36), Hex(0xC97A5C), dawn);
  const Rgba low = Mixed(Hex(0x2A2346), Hex(0xFFD08C), dawn);
  auto vec = [](const Rgba& c) { return glm::vec4(c.r, c.g, c.b, c.a); };
  sky_top_->SetParam("uTop", vec(top));
  sky_top_->SetParam("uBottom", vec(mid));
  sky_top_->SetParam("uRadius", 0.0f);
  sky_low_->SetParam("uTop", vec(mid));
  sky_low_->SetParam("uBottom", vec(low));
  sky_low_->SetParam("uRadius", 0.0f);
  c.ShaderRect({x(0, 0), x(kWidth, 165)}, sky_top_);
  c.ShaderRect({x(0, 165), x(kWidth, 301)}, sky_low_);
  for (const Dot& star : sky_stars_) c.Circle(x(star.at), star.r * x.s, Hex(0xE8EEFF, (1.0f - dawn) * star.alpha));
  if (dawn > 0.0f) {
    const glm::vec2 sun = x(322, 270 - 120 * dawn);
    glow_.Draw(c, sun, 60.0f * x.s, Hex(0xFFD58A, 0.6f * dawn));
    c.Circle(sun, 28.0f * x.s, Hex(0xFFE2A0, dawn));
  }
}

void CityArt::DrawTower(Canvas& c, const Xf& x, float top, float bottom) {
  const float s = x.s;
  c.FillRect({x(0, 300), x(kWidth, 2500)}, kWall);
  if (top < 300.0f) {
    // The roof: slates, a chimney, the spire and its beacon.
    c.FillRect({x(263, 128), x(281, 198)}, kRoofEdge);
    c.FillRect({x(264.5f, 129.5f), x(279.5f, 196.5f)}, kRoof);
    const glm::vec2 a = x(60, 264), b = x(195, 80), d = x(330, 264);
    c.Triangle(a, b, d, kRoof);
    Outline(c, {a, b, d}, 2.0f * s, kRoofEdge);
    for (float y = 110.0f; y < 264.0f; y += 16.0f) {
      const float half = (y - 80.0f) * 135.0f / 184.0f;
      c.Line(x(195 - half + 4, y), x(195 + half - 4, y), 1.0f * s, kSlate);
    }
    c.Line(x(195, 80), x(195, 50), 2.5f * s, kBrass);
  }
  if (top < 290.0f && bottom > 255.0f) {
    c.FillRect({x(8, 262), x(382, 284)}, kBrassDim);
    c.FillRect({x(9, 263), x(381, 283)}, Hex(0x222C40));
  }
  // The tower's face and its courses of stone.
  c.FillRect({x(14, 284), x(376, 2202)}, kTowerFace);
  for (float y = 310.0f; y < std::min(2200.0f, bottom); y += 26.0f) {
    if (y > top - 2.0f) c.Line(x(14, y), x(376, y), 1.0f * s, kCourse);
  }
  // The rose window's iron and dark glass (what lights is drawn each frame).
  if (top < city::kWindow.y + city::kWindowRim && bottom > city::kWindow.y - city::kWindowRim) {
    const glm::vec2 w = x(city::kWindow);
    c.Circle(w, (city::kWindowRim + 1.5f) * s, kBrass);
    c.Circle(w, (city::kWindowRim - 1.5f) * s, Hex(0x1A2333));
    c.Circle(w, (city::kWindowInside + 0.75f) * s, kBrassDim);
    c.Circle(w, (city::kWindowInside - 0.75f) * s, kGlassDark);
    auto canvas_band = [&](const city::Band& band) {
      std::vector<glm::vec2> inner, outer;
      for (Vec2 p : band.inner) inner.push_back(x(p));
      for (Vec2 p : band.outer) outer.push_back(x(p));
      return std::pair{inner, outer};
    };
    for (int p = 0; p < city::kPetals; ++p) {
      const auto [ti, to] = canvas_band(city::PetalTip(p, 8));
      FillBand(c, ti, to, kPane);
      const auto [li, lo] = canvas_band(city::LeanPane(p, 4));
      FillBand(c, li, lo, kSmallPane);
      const auto [si, so] = canvas_band(city::SwiftPane(p, 4));
      FillBand(c, si, so, kSmallPane);
    }
    c.Circle(w, (city::kHeart + 1.25f) * s, kBrass);
    c.Circle(w, (city::kHeart - 1.25f) * s, kGlassDark);
    for (int k = 0; k < city::kLobes; ++k) c.Circle(x(city::LobeCentre(k)), city::kLobeRadius * s, kLobe);
  }
  // The tower's foot: the lancet windows, the door, the ledge.
  if (top < 2220.0f && bottom > 2080.0f) {
    const auto at = [&](float px, float py) { return x(px, py); };
    Arched(c, at, s, 58, 90, 700 + kLow, 776 + kLow, Hex(0x141F31), Hex(0x2A3550), 2.0f);
    Arched(c, at, s, 300, 332, 700 + kLow, 776 + kLow, Hex(0x141F31), Hex(0x2A3550), 2.0f);
    Arched(c, at, s, 170, 220, 762 + kLow, 802 + kLow, kGlassDark, kRoofEdge, 2.0f);
    c.FillRect({x(4, 800 + kLow), x(386, 818 + kLow)}, kRoofEdge);
    c.FillRect({x(5, 801 + kLow), x(385, 817 + kLow)}, kStone);
  }
  // The tower's pipe toward World 2's gate (its light is drawn each frame).
  if (top < city::kTowerPipeBottom.y + 10.0f && bottom > city::kTowerPipeTop.y - 10.0f) {
    c.Line(x(city::kTowerPipeTop), x(city::kTowerPipeBottom), 11.0f * s, kPipeIron);
    c.Line(x(city::kTowerPipeTop), x(city::kTowerPipeBottom), 2.0f * s, Hex(0x3A4A66));
  }
}

void CityArt::DrawTown(Canvas& c, const Xf& x, float top, float bottom) {
  const float s = x.s;
  // The districts' ground.
  c.FillRect({x(0, 1100 + kLow), x(kWidth, 2080 + kLow)}, Hex(0x0B111D));
  c.FillRect({x(0, 2080 + kLow), x(kWidth, 3040 + kLow)}, Hex(0x09101A));
  c.FillRect({x(0, 3040 + kLow), x(kWidth, 4000 + kLow)}, Hex(0x0A0810));
  // The square under the tower, cobbled.
  if (top < 1060 + kLow && bottom > 930 + kLow) {
    c.FillRect({x(0, 930 + kLow), x(kWidth, 1060 + kLow)}, Hex(0x0D1320));
    for (float y = 948.0f; y < 1060.0f; y += 18.0f)
      c.Line(x(0, y + kLow), x(kWidth, y + kLow), 1.0f * s, Hex(0x151D2C));
  }
  // The canals and their bridges.
  for (float wy : {2400.0f, 2730.0f}) {
    const float y = wy + kLow;
    if (top > y + 80.0f || bottom < y - 10.0f) continue;
    const Rgba water = Hex(0x0A1C2A);
    c.FillRect({x(0, y), x(kWidth, y + 70)}, water);
    for (int k = 0; k < 4; ++k) {
      const float ry = y + 22.0f + static_cast<float>(k % 2) * 22.0f, rx = 20.0f + static_cast<float>(k) * 100.0f;
      c.Line(x(rx, ry), x(rx + 40, ry), 1.2f * s, Hex(0x14324A));
    }
    const float bx = wy < 2500.0f ? 70.0f : 230.0f;
    c.FillRect({x(bx - 60, y - 8), x(bx + 90, y + 70)}, Hex(0x2A3A56));
    c.FillRect({x(bx - 58.5f, y - 6.5f), x(bx + 88.5f, y + 70)}, Hex(0x0F1828));
    c.Circle(x(bx + 25, y + 70), 41.5f * s, Hex(0x2A3A56));
    c.Circle(x(bx + 25, y + 70), 40.0f * s, water);
    // Only the arch's upper half shows: the ground under the canal covers the rest.
    c.FillRect({x(bx - 20, y + 70), x(bx + 70, y + 113)}, Hex(0x09101A));
  }
  // Houses and rooftops.
  for (const Shape& shape : shapes_) {
    if (shape.bottom < top || shape.top > bottom) continue;
    std::vector<glm::vec2> points;
    for (Vec2 p : shape.points) points.push_back(x(p));
    FillShape(c, points, shape.fill);
    Outline(c, points, shape.width * s, shape.stroke);
  }
  // The sanctum, deep under the city: strata, a domed hall with its columns and spire.
  if (bottom > 3070 + kLow) {
    for (float y = 3070.0f + kLow; y < 4000.0f + kLow; y += 34.0f) {
      if (y < top - 10.0f || y > bottom + 10.0f) continue;
      std::vector<glm::vec2> wave;
      for (int i = 0; i <= 20; ++i) {
        const float t = static_cast<float>(i) / 20.0f;
        wave.push_back(x(t * kWidth, y + 4.0f * std::sin(t * 2.0f * kPi)));
      }
      art::Polyline(c, wave, 1.2f * s, Hex(0x16121F));
    }
    const float yb = 3640.0f + kLow;
    const Rgba fill = Hex(0x120E1C), edge = Hex(0x2E2846);
    if (top < yb + 40.0f && bottom > yb - 250.0f) {
      c.Circle(x(195, yb - 70), 126.6f * s, edge);
      c.Circle(x(195, yb - 70), 125.0f * s, fill);
      c.FillRect({x(68.4f, yb - 70), x(321.6f, yb)}, edge);
      c.FillRect({x(70, yb - 70), x(320, yb - 1.6f)}, fill);
      art::FillConvex(c, {x(183, yb - 195), x(183, yb - 222), x(195, yb - 240), x(207, yb - 222), x(207, yb - 195)},
                      fill);
      Outline(c, {x(183, yb - 195), x(183, yb - 222), x(195, yb - 240), x(207, yb - 222), x(207, yb - 195)}, 1.6f * s,
              edge);
      for (int k = 0; k < 6; ++k) {
        const float cx = 88.0f + static_cast<float>(k) * 40.0f;
        Outline(c, {x(cx, yb), x(cx, yb - 60), x(cx + 14, yb - 60), x(cx + 14, yb)}, 1.6f * s, edge);
      }
      c.FillRect({x(40, yb + 14), x(350, yb + 30)}, edge);
      c.FillRect({x(41.5f, yb + 15.5f), x(348.5f, yb + 28.5f)}, fill);
    }
  }
  // Each district's dotted path and the pipes between the gates.
  for (int w = 2; w <= city::kDistricts; ++w) {
    const city::District& d = city::Districts()[static_cast<size_t>(w - 1)];
    if (d.bottom < top - 100.0f || d.top > bottom + 300.0f) continue;
    std::vector<glm::vec2> path;
    for (Vec2 p : lamp_paths_[static_cast<size_t>(w - 1)]) path.push_back(x(p));
    Dots(c, path, 11.0f * s, 0.0f, 1.25f * s, Hex(0x25324A));
    std::vector<city::Cubic> pipes{city::GatePipe(w)};
    if (w >= 3) pipes.push_back(city::LinkPipe(w));
    for (const city::Cubic& pipe : pipes) {
      std::vector<glm::vec2> points;
      for (Vec2 p : pipe.Points(24)) points.push_back(x(p));
      art::Polyline(c, points, 9.0f * s, kPipeIron);
      art::Polyline(c, points, 2.0f * s, kPipeShine);
    }
  }
}

// ---- What lights and moves ----

void CityArt::DrawLights(Canvas& p, const Xf& x, const CityFrame& frame) {
  const float s = x.s, t = frame.time, view_bottom = frame.scroll + frame.ViewHeight();
  float lit = 0.0f;
  for (int i = 0; i < city::kPetals; ++i) lit += Clamp01(frame.pane[static_cast<size_t>(i)][0]);
  auto visible = [&](float y0, float y1) { return y1 > frame.scroll - 20.0f && y0 < view_bottom + 20.0f; };
  const auto at = [&](float px, float py) { return x(px, py); };
  if (visible(0.0f, 300.0f)) {
    // Smoke from the chimney, the clock's warm face, the dormer windows: they flicker.
    for (int k = 0; k < 2; ++k) {
      const float age = frame.calm ? 0.5f : std::fmod(t / 4.0f + 0.5f * static_cast<float>(k), 1.0f);
      const float r = k == 0 ? 9.0f : 7.0f, x0 = k == 0 ? 272.0f : 276.0f, y0 = k == 0 ? 118.0f : 112.0f;
      p.Circle(x(x0, y0 - 46.0f * age), r * s, Hex(0x3A4256, 0.45f * (1.0f - age)));
    }
    const float flicker = frame.calm ? 0.85f : 0.78f + 0.12f * std::sin(t * 2.0f) + 0.08f * std::sin(t * 5.3f);
    p.Circle(x(195, 178), 20.0f * s, Hex(0xFFB25A, flicker));
    p.Ring(x(195, 178), 20.0f * s, 3.0f * s, kBrass);
    p.Line(x(187, 178), x(203, 178), 2.0f * s, Hex(0x5A3A16));
    p.Line(x(195, 160), x(195, 196), 2.0f * s, Hex(0x5A3A16));
    Arched(p, at, s, 120, 142, 232, 250, Hex(0xE59A4A, 0.8f * flicker), {}, 0.0f);
    Arched(p, at, s, 248, 270, 232, 250, Hex(0xE59A4A, 0.8f * flicker), {}, 0.0f);
    p.Circle(x(195, 48), 5.0f * s, Hex(0xF6DA9A, 0.3f + 0.7f * city::Dawn(frame.stars)));
  }
  if (visible(2060.0f, 2420.0f)) {
    // The window's light falls on the square and the roofs below; the lancets glow with it.
    glow_.Draw(p, x(195, 880 + kLow), glm::vec2(250.0f, 120.0f) * s,
               WithAlpha(kWarm, 0.75f * (0.06f + 0.6f * lit / 15.0f)));
    const Rgba lancet = WithAlpha(kWarm, 0.05f + 0.4f * lit / 15.0f);
    Arched(p, at, s, 58, 90, 700 + kLow, 776 + kLow, lancet, {}, 0.0f);
    Arched(p, at, s, 300, 332, 700 + kLow, 776 + kLow, lancet, {}, 0.0f);
    static constexpr Vec2 kRoofLights[] = {{18, 892},  {44, 880},  {84, 900},  {110, 886}, {270, 895},
                                           {300, 882}, {336, 898}, {362, 888}, {128, 908}, {258, 910}};
    const int on = static_cast<int>(std::lround(lit * 10.0f / 15.0f));
    for (int k = 0; k < 10; ++k) {
      const Vec2 w = kRoofLights[k];
      p.FillRect({x(w.x, w.y + kLow), x(w.x + 6, w.y + 8 + kLow)}, Hex(0xFFCF7A, k < on ? 0.85f : 0.08f));
    }
  }
  if (visible(3300.0f + kLow, 3900.0f + kLow)) {
    static constexpr Vec2 kCandles[] = {{60, 3500}, {330, 3480}, {120, 3720}, {270, 3740},
                                        {40, 3860}, {350, 3880}, {195, 3780}, {150, 3330}};
    for (int k = 0; k < 8; ++k) {
      const float f = frame.calm ? 0.75f : 0.75f + 0.25f * std::sin(t * 2.0f + static_cast<float>(k) * 1.7f);
      p.Circle(x(kCandles[k].x, kCandles[k].y + kLow), 2.2f * s, Hex(0xE6CC88, 0.5f * f));
    }
  }
  // Light rising down the tower's pipe toward World 2's gate.
  if (frame.tower_fill > 0.0f && visible(city::kTowerPipeTop.y, city::kTowerPipeBottom.y)) {
    const glm::vec2 a = x(city::kTowerPipeTop);
    const glm::vec2 b =
        x(city::kTowerPipeTop.x,
          city::kTowerPipeTop.y + (city::kTowerPipeBottom.y - city::kTowerPipeTop.y) * frame.tower_fill);
    p.Line(a, b, 6.0f * s, kLight);
    Dots(p, {a, b}, 15.0f * s, (frame.calm ? 0.0f : t * 33.0f) * s, 1.5f * s, kBead);
  }
}

void CityArt::DrawMist(Canvas& p, const Xf& x, const CityFrame& frame) {
  static constexpr float kBands[3][3] = {{1060, 1150, 1200}, {2010, 2110, 2150}, {2980, 3060, 3110}};
  const float view_bottom = frame.scroll + frame.ViewHeight();
  for (int m = 0; m < 3; ++m) {
    const float y = kBands[m][0] + kLow;
    if (y > view_bottom + 20.0f || y + 220.0f < frame.scroll - 20.0f) continue;
    // Thinner over a district that has woken.
    const float thick = frame.districts[static_cast<size_t>(m)].open ? 0.45f : 1.0f;
    for (int k = 0; k < 22; ++k) {
      const float f = (static_cast<float>(k) + 0.5f) / 22.0f;
      const float a = 0.8f * (1.0f - std::fabs(f * 2.0f - 1.0f));
      p.FillRect({x(0, y + 10.0f * static_cast<float>(k)), x(kWidth, y + 10.0f * static_cast<float>(k + 1))},
                 Hex(0x1A2338, a * thick));
    }
    const float drift = frame.calm ? 0.0f : 22.0f * std::sin(frame.time * 2.0f * kPi / 14.0f);
    const float drift2 = frame.calm ? 0.0f : -22.0f * std::sin(frame.time * 2.0f * kPi / 19.0f);
    wlw::ui::ImageStyle style;
    style.tint = Hex(0x2A3754, 0.55f * thick);
    const glm::vec2 c1 = x(110 + drift, kBands[m][1] + kLow), c2 = x(290 + drift2, kBands[m][2] + kLow);
    p.Image({c1 - glm::vec2(170, 60) * x.s, c1 + glm::vec2(170, 60) * x.s}, soft_, style);
    p.Image({c2 - glm::vec2(190, 70) * x.s, c2 + glm::vec2(190, 70) * x.s}, soft_, style);
  }
}

void CityArt::DrawDistricts(Canvas& p, Canvas& c, const Xf& x, const CityFrame& frame) {
  const float s = x.s, t = frame.time, view_bottom = frame.scroll + frame.ViewHeight();
  const wlw::ui::Font& font = *frame.font;
  for (int w = 2; w <= city::kDistricts; ++w) {
    const city::District& d = city::Districts()[static_cast<size_t>(w - 1)];
    const CityFrame::District& df = frame.districts[static_cast<size_t>(w - 2)];
    if (d.valve - 60.0f > view_bottom || d.bottom < frame.scroll) continue;
    // Light poured down the gate's pipe.
    if (df.pour > 0.0f) {
      std::vector<glm::vec2> points;
      for (Vec2 q : city::GatePipe(w).Points(32)) points.push_back(x(q));
      const art::Path path = art::MakePath(points);
      const art::Path part = path.Part(0.0f, path.Length() * Clamp01(df.pour));
      art::Stroke(p, part, 0.0f, 5.0f * s, kLight);
      if (df.pour >= 1.0f) Dots(p, points, 15.0f * s, (frame.calm ? 0.0f : t * 33.0f) * s, 1.5f * s, kBead);
    }
    // The lamps: asleep (dark), open (a ring and its number), lit (its level solved).
    const auto lamps = city::DistrictLamps(w);
    for (int j = 0; j < city::kDistrictLamps; ++j) {
      const city::Light& light = df.lamps[static_cast<size_t>(j)];
      const bool challenge = j >= 15;
      const glm::vec2 at = x(lamps[static_cast<size_t>(j)]);
      float awake = (light.bits & kSolveStar) ? 1.0f : 0.0f;
      if (j == 0 && df.wake > 0.0f && df.wake < 1.0f)
        awake = std::max(awake, std::sin(df.wake * kPi));  // the gate opening
      const bool open = !light.locked;
      const float r = (open ? (challenge ? 13.0f : 12.0f) : (challenge ? 8.0f : 7.0f)) * s;
      if (awake > 0.0f) glow_.Draw(p, at, (10.0f + 26.0f * awake) * s, WithAlpha(kLight, 0.9f * awake));
      const Rgba fill = Mixed(Hex(0x101826), Hex(0xFFD47A), awake);
      const Rgba stroke = awake > 0.0f ? kBrassLit : open ? kBrass : challenge ? Hex(0x3A3550) : Hex(0x2C3B55);
      if (challenge) {
        const std::vector<glm::vec2> diamond = DiamondPoints(at, r + 2.0f * s);
        art::FillConvex(p, diamond, stroke);
        art::FillConvex(p, DiamondPoints(at, r), fill);
      } else {
        p.Circle(at, r + 2.0f * s, stroke);
        p.Circle(at, r, fill);
      }
      if (open) {
        const std::string number = challenge ? "C" + std::to_string(j - 14) : std::to_string(j + 1);
        c.Text(font, number, at, (challenge ? 11.0f : 13.0f) * kText * s, Align::Center,
               awake > 0.5f ? wlw::ui::TextStyle{.color = Hex(0x2A1A00)} : Outlined(ui_color::kInk));
      }
      if (j == df.current) {
        const float o = frame.calm ? 0.8f : 0.6f + 0.4f * std::sin(t * 2.0f * kPi / 2.4f);
        p.Ring(at, r + 7.0f * s, 3.0f * s, WithAlpha(kBrassLit, o));
      }
    }
    // The gate: a brass valve on the pipe, its plate saying how many stars open it.
    const float vy = d.valve;
    const glm::vec2 v = x(195, vy);
    RoundRect(p, {x(181, vy - 22), x(209, vy + 22)}, 5.0f * s, Hex(0x2A2116), Hex(0x8A6A3A), 1.5f * s);
    const float turn = (df.open ? 1.0f : df.valve) * 1.5f * kPi;  // 270 degrees
    p.Ring(v, 19.0f * s, 4.0f * s, kBrass);
    for (int k = 0; k < 2; ++k) {
      const float a = turn + static_cast<float>(k) * kPi * 0.5f;
      const glm::vec2 dir{std::sin(a), -std::cos(a)};
      p.Line(v - dir * 17.0f * s, v + dir * 17.0f * s, 3.0f * s, kBrass);
    }
    p.Circle(v, 5.0f * s, Hex(0xE9C27A));
    const bool ready = df.open || df.have >= df.gate;
    RoundRect(c, {x(224, vy - 14), x(284, vy + 14)}, 6.0f * s, Hex(0x1A1408), ready ? kGold : kBrassDim, 1.5f * s);
    const Rgba label = ready ? Hex(0xFFE6A6) : kBrass;
    if (df.open) {
      c.Text(font, "OPEN", x(254, vy), 15.0f * kText * s, Align::Center, {.color = label});
    } else {
      const std::string n = std::to_string(df.gate);
      const float wn = font.Measure(n, 15.0f * kText * s);
      const glm::vec2 mid = x(254, vy);
      c.Text(font, n, {mid.x - (wn + 15.0f * s) * 0.5f, mid.y}, 15.0f * kText * s, Align::Left, {.color = label});
      StarShape(c, {mid.x + (wn + 15.0f * s) * 0.5f - 6.0f * s, mid.y}, 6.5f * s, label);
    }
    // The district's name plate.
    const float py = d.plate;
    RoundRect(c, {x(92, py), x(298, py + 54)}, 9.0f * s, Hex(0x0B111C), kBrassDim, 1.5f * s);
    Title(c, font, std::to_string(w), df.name, x(195, py + 18), 18.0f * kText * s, Hex(0xC9B48A));
    c.Text(font, d.place, x(195, py + 39), 12.5f * kText * s, Align::Center, {.color = Hex(0x7C8AA3)});
  }
}

void CityArt::DrawWindow(Canvas& p, const Xf& x, const CityFrame& frame) {
  const float s = x.s;
  if (frame.scroll > city::kWindow.y + city::kWindowRim) return;
  const Palette& palette = *frame.palette;
  auto band = [&](const city::Band& b) {
    std::pair<std::vector<glm::vec2>, std::vector<glm::vec2>> out;
    for (Vec2 q : b.inner) out.first.push_back(x(q));
    for (Vec2 q : b.outer) out.second.push_back(x(q));
    return out;
  };
  for (int i = 0; i < city::kPetals; ++i) {
    const auto& lit = frame.pane[static_cast<size_t>(i)];
    const auto& flash = frame.pane_flash[static_cast<size_t>(i)];
    const bool perfect = lit[0] >= 1.0f && lit[1] >= 1.0f && lit[2] >= 1.0f;
    const Rgba lead = frame.locked[static_cast<size_t>(i)] ? kLeadLocked
                      : perfect                            ? kGold
                      : lit[0] > 0.0f                      ? kLeadLit
                                                           : kLeadDark;
    const float lead_w = (perfect ? 2.8f : 2.0f) * s;
    const std::vector<Color>& colors = frame.colors[static_cast<size_t>(i)];
    const int n = std::max(1, static_cast<int>(colors.size()));
    const auto [ti, to] = band(city::PetalTip(i, 8));
    if (lit[0] > 0.0f) {
      for (int k = 0; k < n; ++k) {
        const Rgba color = colors.empty() ? kLight : LiquidColor(palette, colors[static_cast<size_t>(k)]);
        const auto [si, so] = band(city::PetalSliver(i, k, n, 4));
        FillBand(p, si, so, WithAlpha(color, Clamp01(lit[0])));
        if (k > 0) p.Line(si.front(), so.front(), 1.4f * s, lead);
      }
      const Vec2 middle = city::Polar(136.0f, city::PetalAngle(i));
      glow_.Draw(p, x(middle), 46.0f * s, WithAlpha(kWarm, 0.22f * Clamp01(lit[0])));
    }
    const auto [li, lo] = band(city::LeanPane(i, 4));
    const auto [wi, wo] = band(city::SwiftPane(i, 4));
    if (lit[1] > 0.0f) FillBand(p, li, lo, WithAlpha(kLeanLight, Clamp01(lit[1])));
    if (lit[2] > 0.0f) FillBand(p, wi, wo, WithAlpha(kSwiftLight, Clamp01(lit[2])));
    if (flash[0] > 0.0f) FillBand(p, ti, to, Hex(0xFFFFFF, flash[0]));
    if (flash[1] > 0.0f) FillBand(p, li, lo, Hex(0xFFFFFF, flash[1]));
    if (flash[2] > 0.0f) FillBand(p, wi, wo, Hex(0xFFFFFF, flash[2]));
    Outline(p, BandOutline(ti, to), lead_w, lead);
    Outline(p, BandOutline(li, lo), lead_w, lead);
    Outline(p, BandOutline(wi, wo), lead_w, lead);
  }
  // The heart's lobes light with the challenges.
  for (int k = 0; k < city::kLobes; ++k) {
    const size_t i = static_cast<size_t>(city::kPetals + k);
    const float a = Clamp01(frame.pane[i][0]);
    const bool perfect = a >= 1.0f && frame.pane[i][1] >= 1.0f && frame.pane[i][2] >= 1.0f;
    const Rgba lead = frame.locked[i] ? kLeadLocked : perfect ? kGold : a > 0.0f ? kLeadLit : kLeadDark;
    const glm::vec2 c = x(city::LobeCentre(k));
    if (a > 0.0f) {
      p.Circle(c, city::kLobeRadius * s, Hex(0xF2F6FF, 0.9f * a));
      glow_.Draw(p, c, 34.0f * s, Hex(0xF2F6FF, 0.25f * a));
    }
    if (frame.pane_flash[i][0] > 0.0f) p.Circle(c, city::kLobeRadius * s, Hex(0xFFFFFF, frame.pane_flash[i][0]));
    p.Ring(c, city::kLobeRadius * s, (perfect ? 2.8f : 2.0f) * s, lead);
  }
  p.Circle(x(city::kWindow), 7.0f * s, kBrass);
}

void CityArt::DrawLanterns(Canvas& p, Canvas& c, const Xf& x, const CityFrame& frame) {
  const float s = x.s, t = frame.time;
  const float view_bottom = frame.scroll + frame.ViewHeight();
  if (frame.scroll > 2100.0f || view_bottom < 600.0f) return;
  const Palette& palette = *frame.palette;
  const wlw::ui::Font& font = *frame.font;
  // The trail: each piece lights once the lantern it leaves from is lit.
  for (size_t piece = 0; piece < trail_.size(); ++piece) {
    const int from = city::Trail()[piece].from;
    std::vector<glm::vec2> points;
    for (Vec2 q : trail_[piece]) points.push_back(x(q));
    Dots(p, points, 11.0f * s, 5.5f * s, 2.5f * s,
         frame.lantern[static_cast<size_t>(from)][0] > 0.0f ? Hex(0xE9C27A) : kLeadDark);
  }
  for (int i = 0; i < city::kLanterns; ++i) {
    const city::Lantern& l = city::Lanterns()[static_cast<size_t>(i)];
    if (l.at.y + 60.0f < frame.scroll || l.at.y - 60.0f > view_bottom) continue;
    const auto& a = frame.lantern[static_cast<size_t>(i)];
    const bool locked = frame.locked[static_cast<size_t>(i)];
    const bool perfect = a[0] >= 1.0f && a[1] >= 1.0f && a[2] >= 1.0f;
    const glm::vec2 at = x(l.at);
    if (a[0] > 0.0f) glow_.Draw(p, at, 52.0f * s, WithAlpha(kLight, 0.55f * Clamp01(a[0])));
    if (!l.diamond) {
      const Rgba cap = locked ? Hex(0x1E2840) : a[0] > 0.0f ? kBrass : kBrassDim;
      art::FillConvex(p,
                      {x(l.at.x - 10, l.at.y - 25), x(l.at.x + 10, l.at.y - 25), x(l.at.x + 6, l.at.y - 32),
                       x(l.at.x - 6, l.at.y - 32)},
                      cap);
    }
    const std::vector<glm::vec2> body = l.diamond ? DiamondPoints(at, l.radius * s) : CirclePoints(at, l.radius * s);
    FillShape(p, body, locked ? Hex(0x0B111C) : kPane);
    // Lit: the level's colours, a strip per cup.
    const std::vector<Color>& colors = frame.colors[static_cast<size_t>(i)];
    if (a[0] > 0.0f && !colors.empty()) {
      const float n = static_cast<float>(colors.size());
      for (size_t k = 0; k < colors.size(); ++k) {
        const float x0 = at.x + (-32.0f + 64.0f * static_cast<float>(k) / n) * s;
        const float x1 = at.x + (-32.0f + 64.0f * static_cast<float>(k + 1) / n) * s;
        const std::vector<glm::vec2> strip = ClipSlab(body, x0, x1);
        if (strip.size() >= 3) FillShape(p, strip, WithAlpha(LiquidColor(palette, colors[k]), Clamp01(a[0])));
      }
    }
    const float flash = frame.lantern_flash[static_cast<size_t>(i)][0];
    if (flash > 0.0f) art::FillConvex(p, body, Hex(0xFFFFFF, flash));
    const Rgba ring = locked ? Hex(0x1E2840) : perfect ? kBrassLit : a[0] > 0.0f ? kBrass : kBrassDim;
    Outline(p, body, (perfect ? 3.5f : 2.5f) * s, ring);

    // The number, and its stars under it (not while locked).
    const std::string number = i < city::kPetals ? std::to_string(i + 1) : "C" + std::to_string(i - city::kPetals + 1);
    const float size = (l.diamond ? 16.0f : 20.0f) * kText * s;
    if (locked) {
      c.Text(font, number, at, size, Align::Center, {.color = Rgba{0.67f, 0.75f, 0.86f, 0.25f}});
    } else if (a[0] > 0.0f) {
      c.Text(font, number, at, size, Align::Center, Outlined(Hex(0xFFFFFF)));
    } else {
      c.Text(font, number, at, size, Align::Center, {.color = Hex(0xC9D6EA)});
    }
    if (locked) continue;
    for (int k = 0; k < 3; ++k) {
      const glm::vec2 star = x(l.at.x + static_cast<float>(k - 1) * city::kStarSpacing, l.stars_y);
      StarShape(c, star, (city::kStarRadius + 1.0f) * s, Hex(0x4A6280));
      StarShape(c, star, city::kStarRadius * s, Hex(0x26364C));
      const float lit = Clamp01(a[static_cast<size_t>(k)]);
      if (lit > 0.0f) {
        const float pop = lit < 1.0f ? 1.0f + 0.7f * std::sin(lit * kPi) : 1.0f;
        StarShape(c, star, (city::kStarRadius + 1.0f) * pop * s, Hex(0xFFE08A, lit));
        StarShape(c, star, city::kStarRadius * pop * s, Hex(0xFFC53D, lit));
      }
    }
  }
  // The next level to play breathes.
  if (frame.breathe >= 0 && frame.breathe < city::kLanterns) {
    const city::Lantern& l = city::Lanterns()[static_cast<size_t>(frame.breathe)];
    const float period = frame.pulse ? 0.7f : 2.4f;
    const float o = frame.calm ? 0.8f : 0.6f - 0.4f * std::cos(t * 2.0f * kPi / period);
    const glm::vec2 at = x(l.at);
    const std::vector<glm::vec2> ring = l.diamond ? DiamondPoints(at, l.ring * s) : CirclePoints(at, l.ring * s, 36);
    Outline(p, ring, 3.5f * s, WithAlpha(kBrassLit, o));
  }
}

}  // namespace threadlock
