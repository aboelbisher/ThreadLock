#include "city_art.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

#include "canal_palette.h"
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

constexpr float kLow = 1400.0f;     // everything under the tower stands this much lower than in the first mock
constexpr float kShop = 284.0f;     // World 2's shop front made the map this much taller (3f)
constexpr float kQuarter = 284.0f;  // World 3's quarter made it taller (the mist bands)
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
// The market square (World 2): wood, copper and the shop's light.
constexpr Rgba kWood = Hex(0x2A1D13);
constexpr Rgba kPlank = Hex(0x3D2C1F);
constexpr Rgba kPlankLine = Hex(0x33241A);
constexpr Rgba kCopper = Hex(0xC86E3A);
constexpr Rgba kCopperDark = Hex(0x8A4A26);
constexpr Rgba kShelf = Hex(0x8A6038);
constexpr Rgba kParchment = Hex(0xF3E8CC);
constexpr Rgba kAmberLit = Hex(0xFFB45C);
constexpr Rgba kShopBrass = Hex(0xC9974E);
// The canal quarter (World 3; its city's colours are the boards', canal_palette.h): the night, the plate, the
// harbours' lamps and the light down the canal.
constexpr Rgba kQuarterNight = Hex(0x0A101B);
constexpr Rgba kPlateInk = Hex(0xEEF4FF), kPlateDim = Hex(0x7F8BA6), kTeal = Hex(0x7FE3D8), kTealLit = Hex(0xB8FAF2);
constexpr Rgba kBasinWater = Hex(0x0E2742), kBasinLocked = Hex(0x0A1424);
constexpr Rgba kHarbourRing = Hex(0x8A9AC0), kHarbourLocked = Hex(0x2C3550), kHarbourPerfect = Hex(0xFFE6A6);
constexpr Rgba kCanalLit = Hex(0x1E5A7A), kBargeAmber = Hex(0xFFC53D), kBargeBlue = Hex(0x4D8DFF);
constexpr Rgba kHoodWarm = canal::color::kWarm;  // a lit neighbourhood
constexpr float kBargeSpeed = 25.0f;             // map units a second
// The sanctum (World 4): its floor and the glow on it, the slabs' joints, the sigil's gold, a lit stroke, the elixir,
// the hall's dark and edges.
constexpr Rgba kSanctumFloor = Hex(0x08070C), kFloorGlow = Hex(0x1B1526), kSlab = Hex(0x8A78B8, 0.07f);
constexpr Rgba kSigilGold = Hex(0xC9A85A), kStrokeLit = Hex(0xFFE9A8), kElixir = Hex(0x3FE0A0);
constexpr Rgba kElixirLight = Hex(0xC8FFE6);
constexpr Rgba kHallDark = Hex(0x120E1A), kHallEdge = Hex(0x3A3048), kHallDim = Hex(0x5E5670);

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

// The sanctum mock's random numbers (mulberry32), so its runes stand where it drew them.
class Mulberry32 {
 public:
  explicit Mulberry32(uint32_t seed) : s_(seed) {}
  float operator()() {
    s_ += 0x6D2B79F5u;
    uint32_t t = (s_ ^ (s_ >> 15)) * (1u | s_);
    t = (t + (t ^ (t >> 7)) * (61u | t)) ^ t;
    return static_cast<float>(static_cast<double>(t ^ (t >> 14)) / 4294967296.0);
  }

 private:
  uint32_t s_;
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

// A convex polygon cut to the slab x0 <= x <= x1 (Sutherland-Hodgman, two planes); axis 1 = a slab in y.
std::vector<glm::vec2> ClipSlab(const std::vector<glm::vec2>& polygon, float x0, float x1, int axis = 0) {
  auto clip = [axis](const std::vector<glm::vec2>& in, float edge, bool keep_above) {
    std::vector<glm::vec2> out;
    for (size_t i = 0; i < in.size(); ++i) {
      const glm::vec2 a = in[i], b = in[(i + 1) % in.size()];
      const bool a_in = keep_above ? a[axis] >= edge : a[axis] <= edge;
      const bool b_in = keep_above ? b[axis] >= edge : b[axis] <= edge;
      if (a_in) out.push_back(a);
      if (a_in != b_in) out.push_back(a + (b - a) * ((edge - a[axis]) / (b[axis] - a[axis])));
    }
    return out;
  };
  return clip(clip(polygon, x0, true), x1, false);
}

// A quadratic Bezier's points after `from` (steps of them, ending at `to`).
void Quad(std::vector<glm::vec2>& out, glm::vec2 from, glm::vec2 control, glm::vec2 to, int steps = 4) {
  for (int k = 1; k <= steps; ++k) {
    const float t = static_cast<float>(k) / static_cast<float>(steps), u = 1.0f - t;
    out.push_back(from * (u * u) + control * (2.0f * u * t) + to * (t * t));
  }
}

std::string Upper(std::string text) {
  for (char& ch : text) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
  return text;
}

float Ease(float k) { return k < 0.5f ? 2.0f * k * k : 1.0f - (2.0f - 2.0f * k) * (2.0f - 2.0f * k) / 2.0f; }

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

// Letter-spaced lettering (the canal quarter's plate): `spacing` canvas units between letters.
float SpacedWidth(const wlw::ui::Font& font, const std::string& text, float size, float spacing) {
  float width = 0.0f;
  for (const char ch : text) width += font.Measure(std::string(1, ch), size) + spacing;
  return text.empty() ? 0.0f : width - spacing;
}

void Spaced(Canvas& c, const wlw::ui::Font& font, const std::string& text, float left, float y, float size,
            float spacing, const Rgba& color) {
  for (const char ch : text) {
    const std::string letter(1, ch);
    c.Text(font, letter, {left, y}, size, Align::Left, {.color = color});
    left += font.Measure(letter, size) + spacing;
  }
}

// Dashes `on` long every `period` along a path in map units, the first `phase` in, round ended; only those near
// map y0..y1 (the view). `shift` moves them off the line.
template <typename At>
void Dashes(Canvas& c, const At& at, float s, const art::Path& path, float on, float period, float phase, float width,
            const Rgba& color, float y0, float y1, glm::vec2 shift = {}) {
  const float length = path.Length();
  for (float a = std::fmod(phase, period) - period; a < length; a += period) {
    const float from = std::max(0.0f, a), to = std::min(length, a + on);
    if (to - from < 0.5f) continue;
    const float y = path.PointAt(from).y;
    if (y < y0 - 40.0f || y > y1 + 40.0f) continue;
    std::vector<glm::vec2> points;
    for (const glm::vec2 q : path.Part(from, to).points) points.push_back(at(q.x + shift.x, q.y + shift.y));
    art::Polyline(c, points, width * s, color);
    if (width >= 3.0f) {
      c.Circle(points.front(), width * 0.5f * s, color);
      c.Circle(points.back(), width * 0.5f * s, color);
    }
  }
}

// A dashed straight line, `on` long every `period` (canvas units).
void DashedLine(Canvas& c, glm::vec2 a, glm::vec2 b, float on, float period, float width, const Rgba& color) {
  const float length = glm::distance(a, b);
  if (length <= 0.0f || period <= 0.0f) return;
  const glm::vec2 u = (b - a) / length;
  for (float d = 0.0f; d < length; d += period) c.Line(a + u * d, a + u * std::min(length, d + on), width, color);
}

// A dashed ring (canvas units).
void DashedRing(Canvas& c, glm::vec2 centre, float r, float on, float period, float width, const Rgba& color) {
  if (r <= 0.0f) return;
  for (float d = 0.0f; d + 0.01f < 2.0f * kPi * r; d += period) c.Arc(centre, r, width, d / r, on / r, color);
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
  // Rooftops round the tower's foot, in the order the mock drew them (they share one sequence of random
  // numbers: the market square's old houses still draw theirs, unkept).
  Rng r(11);
  auto houses = [&](float yb, float x0, float x1, float w_min, float w_span, float h_min, float h_span, Rgba fill,
                    Rgba stroke, float width, bool keep = true) {
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
      if (keep) shapes_.push_back(std::move(shape));
      x += w + 2.0f + r() * 6.0f;
    }
  };
  houses(930 + kLow, -12, 140, 34, 22, 40, 50, Hex(0x0F1624), Hex(0x26324A), 1.5f);
  houses(930 + kLow, 252, 400, 34, 22, 40, 50, Hex(0x0F1624), Hex(0x26324A), 1.5f);
  for (float yb : {1420.0f, 1640.0f, 1860.0f, 2060.0f})
    houses(yb + kLow, -10, 400, 38, 26, 34, 40, Hex(0x0D1524), Hex(0x22314A), 1.2f, false);

  Rng stars(5);
  for (int k = 0; k < 30; ++k) {
    Dot dot;
    dot.at.x = stars() * 390.0f;
    dot.at.y = 62.0f + stars() * 200.0f;
    dot.r = 0.6f + stars() * 1.2f;
    dot.alpha = 0.4f + stars() * 0.6f;
    sky_stars_.push_back(dot);
  }

  std::vector<glm::vec2> canal;
  for (const Vec2 q : city::CanalPart(city::CanalLength(), 2.5f)) canal.push_back({q.x, q.y});
  grand_canal_ = art::MakePath(std::move(canal));
  // The sanctum's runes round its sigil, between the gold rings (the mock's random numbers, so they stand where it
  // drew them), and the groove down its stair.
  Mulberry32 rune(4);
  for (int i = 0; i < 60; ++i) {
    const float a = static_cast<float>(i * 6 + 3) * kPi / 180.0f;
    const Vec2 c{city::kSanctumCentre.x + std::cos(a) * 172.0f, city::kSanctumCentre.y + std::sin(a) * 172.0f};
    const glm::vec2 t{-std::sin(a), std::cos(a)}, n{std::cos(a), std::sin(a)};
    const int strokes = 2 + static_cast<int>(rune() * 2.0f);
    for (int k = 0; k < strokes; ++k) {
      const float u0 = (rune() - 0.5f) * 6.0f;
      const float v0 = (rune() - 0.5f) * 7.0f;
      const float u1 = (rune() - 0.5f) * 6.0f;
      const float v1 = (rune() - 0.5f) * 7.0f;
      runes_.push_back({Vec2{c.x + t.x * u0 + n.x * v0 * 0.5f, c.y + t.y * u0 + n.y * v0 * 0.5f},
                        Vec2{c.x + t.x * u1 + n.x * v1 * 0.5f, c.y + t.y * u1 + n.y * v1 * 0.5f}});
    }
  }
  groove_ = art::MakePath({{195.0f, city::kSanctumGrooveTop}, {195.0f, city::kSanctumGrooveBottom}});
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
  DrawMarket(picture, canvas, x, frame);
  DrawQuarter(picture, canvas, x, frame);
  DrawSanctum(picture, canvas, x, frame);
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
  DrawMarketStill(c, x, top, bottom);
  DrawQuarterStill(c, x, top, bottom);
  DrawSanctumStill(c, x, top, bottom);
  // The square under the tower, cobbled.
  if (top < 1060 + kLow && bottom > 930 + kLow) {
    c.FillRect({x(0, 930 + kLow), x(kWidth, 1060 + kLow)}, Hex(0x0D1320));
    for (float y = 948.0f; y < 1060.0f; y += 18.0f)
      c.Line(x(0, y + kLow), x(kWidth, y + kLow), 1.0f * s, Hex(0x151D2C));
  }
  // Houses and rooftops.
  for (const Shape& shape : shapes_) {
    if (shape.bottom < top || shape.top > bottom) continue;
    std::vector<glm::vec2> points;
    for (Vec2 p : shape.points) points.push_back(x(p));
    FillShape(c, points, shape.fill);
    Outline(c, points, shape.width * s, shape.stroke);
  }
  // The pipes between the gates.
  for (int w = 2; w <= city::kDistricts; ++w) {
    const city::District& d = city::Districts()[static_cast<size_t>(w - 1)];
    if (d.bottom < top - 100.0f || d.top > bottom + 300.0f) continue;
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
  static constexpr float kBands[3][3] = {{1060, 1150, 1200},
                                         {2010 + kShop, 2110 + kShop, 2150 + kShop},
                                         {2980 + kShop + kQuarter, 3060 + kShop + kQuarter, 3110 + kShop + kQuarter}};
  const float view_bottom = frame.scroll + frame.ViewHeight();
  for (int m = 0; m < 3; ++m) {
    const float y = kBands[m][0] + kLow;
    if (y > view_bottom + 20.0f || y + 220.0f < frame.scroll - 20.0f) continue;
    // The market's, the canal quarter's and the sanctum's lift as they wake.
    const float thick = m == 0   ? 1.0f - frame.market.awake
                        : m == 1 ? 1.0f - frame.quarter.awake
                                 : 1.0f - frame.sanctum.awake;
    if (thick <= 0.0f) continue;
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
    if (w == 2) {  // the market's: wood and copper, the place's name large
      RoundRect(c, {x(70, py), x(320, py + 50)}, 6.0f * s, kWood, kCopper, 1.5f * s);
      Title(c, font, "2", Upper(df.name), x(195, py + 16), 11.0f * kText * s, kAmberLit);
      c.Text(font, d.place, x(195, py + 35), 15.0f * kText * s, Align::Center, {.color = kParchment});
      continue;
    }
    if (w == 3 && frame.street) {  // the quarter's: night blue and silver, its name letter-spaced, lit as it wakes
      const city::Box& b = city::kQuarterPlate;
      const float awake = frame.quarter.awake;
      RoundRect(c, {x(b.x0, b.y0), x(b.x1, b.y1)}, 6.0f * s, Hex(0x141C2E), Hex(0xB9C6E2), 1.5f * s);
      Outline(c, {x(b.x0 + 6, b.y0 + 6), x(b.x1 - 6, b.y0 + 6), x(b.x1 - 6, b.y1 - 6), x(b.x0 + 6, b.y1 - 6)}, 1.0f * s,
              canal::color::kQuay);
      const std::string place = "The Canal Quarter";  // Cormorant SC: small capitals
      float size = 19.0f * kText * s, spacing = 3.0f * s;
      const float wide = SpacedWidth(*frame.street, place, size, spacing), room = (b.x1 - b.x0 - 24.0f) * s;
      if (wide > room) size *= room / wide, spacing *= room / wide;
      const glm::vec2 big = x(195, b.y0 + 22);
      Spaced(c, *frame.street, place, big.x - SpacedWidth(*frame.street, place, size, spacing) * 0.5f, big.y, size,
             spacing, Mixed(kPlateDim, kPlateInk, awake));
      // "3 · RUSH HOUR": the world's number and name, small, teal.
      const float small = 9.5f * kText * s, gap = small * 0.55f, tracking = 1.5f * s;
      const std::string number = std::to_string(w), name = Upper(df.name);
      const float wn = SpacedWidth(font, number, small, tracking), wt = SpacedWidth(font, name, small, tracking);
      const glm::vec2 line = x(195, b.y0 + 40);
      const float left = line.x - (wn + gap + wt) * 0.5f;
      const Rgba teal = WithAlpha(kTeal, 0.4f + 0.6f * awake);
      Spaced(c, font, number, left, line.y, small, tracking, teal);
      c.Circle({left + wn + gap * 0.5f, line.y}, small * 0.09f, teal);
      Spaced(c, font, name, left + wn + gap, line.y, small, tracking, teal);
      continue;
    }
    if (w == 4 && frame.sigil) {  // the sanctum's: violet and old gold, its name in Cinzel, lit as it wakes
      const city::Box& b = city::kSanctumPlate;
      const float awake = frame.sanctum.awake;
      RoundRect(c, {x(b.x0, b.y0), x(b.x1, b.y1)}, 6.0f * s, Hex(0x140F1E), Hex(0xD8C8A0), 1.5f * s);
      Outline(c, {x(b.x0 + 6, b.y0 + 6), x(b.x1 - 6, b.y0 + 6), x(b.x1 - 6, b.y1 - 6), x(b.x0 + 6, b.y1 - 6)}, 1.0f * s,
              Hex(0x4A3E5E));
      const std::string place = "THE SANCTUM";  // Cinzel: capitals
      float size = 18.0f * kText * s, spacing = 3.0f * s;
      const float wide = SpacedWidth(*frame.sigil, place, size, spacing), room = (b.x1 - b.x0 - 24.0f) * s;
      if (wide > room) size *= room / wide, spacing *= room / wide;
      const glm::vec2 big = x(195, b.y0 + 24);
      Spaced(c, *frame.sigil, place, big.x - SpacedWidth(*frame.sigil, place, size, spacing) * 0.5f, big.y, size,
             spacing, Mixed(Hex(0x7F7898), Hex(0xF4EEDD), awake));
      // "4 · THE MIXING HALL": the world's number and name, small, old gold.
      const float small = 9.5f * kText * s, gap = small * 0.55f, tracking = 1.5f * s;
      const std::string number = std::to_string(w), name = Upper(df.name);
      const float wn = SpacedWidth(font, number, small, tracking), wt = SpacedWidth(font, name, small, tracking);
      const glm::vec2 line = x(195, b.y0 + 40);
      const float left = line.x - (wn + gap + wt) * 0.5f;
      const Rgba gold = WithAlpha(Hex(0xE6CC88), 0.4f + 0.6f * awake);
      Spaced(c, font, number, left, line.y, small, tracking, gold);
      c.Circle({left + wn + gap * 0.5f, line.y}, small * 0.09f, gold);
      Spaced(c, font, name, left + wn + gap, line.y, small, tracking, gold);
      continue;
    }
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

// ---- The market square (World 2) ----

void CityArt::DrawMarketStill(Canvas& c, const Xf& x, float top, float bottom) {
  const float s = x.s;
  constexpr float kGround = 2460.0f, kWarmest = 2794.0f, kEnd = 3764.0f;  // the market's ground, map y
  if (bottom < kGround || top > kEnd) return;
  // The ground: the night's blue under the gate warming to the shop's brown, darker again down the square.
  constexpr float kStrip = 6.0f;
  for (float y = kGround + std::max(0.0f, std::floor((top - kGround) / kStrip)) * kStrip; y < std::min(kEnd, bottom);
       y += kStrip) {
    const float mid = y + kStrip * 0.5f;
    const Rgba ground = mid < kWarmest ? Mixed(Hex(0x0C121E), Hex(0x2A1C14), (mid - kGround) / (kWarmest - kGround))
                                       : Mixed(Hex(0x2A1C14), Hex(0x1A120C), (mid - kWarmest) / (kEnd - kWarmest));
    // Past the fountain it fades into the canal quarter's night.
    const float night = Clamp01((mid - city::kMarketBottom) / (kEnd - city::kMarketBottom));
    c.FillRect({x(0, y), x(kWidth, std::min(y + kStrip + 0.5f, kEnd))}, Mixed(ground, kQuarterNight, Smooth(night)));
  }
  // The cobbles, each row offset by half a stone.
  int row = 0;
  for (float y = 2974.0f; y < city::kMarketBottom; y += 15.0f, ++row) {
    if (y < top - 2.0f || y > bottom + 2.0f) continue;
    for (float sx = static_cast<float>(row % 2) * 13.0f - 13.0f; sx < kWidth; sx += 26.0f)
      c.Line(x(sx + 2, y), x(sx + 22, y), 1.0f * s, kWood);
  }
  // Three stalls at the square's edges under striped awnings.
  struct Stall {
    float x, y;
  };
  for (const Stall st : {Stall{-4, 3002}, Stall{354, 3132}, Stall{-4, 3262}}) {
    if (st.y + 40.0f < top || st.y - 20.0f > bottom) continue;
    RoundRect(c, {x(st.x, st.y), x(st.x + 40, st.y + 32)}, 0.0f, kPlank, kWood, 1.0f * s);
    const float ay = st.y - 16.0f;
    for (int i = 0; i < 5; ++i) {
      const float x0 = st.x + 9.0f * static_cast<float>(i);
      std::vector<glm::vec2> scallop{x(x0, ay), x(x0 + 9, ay), x(x0 + 9, ay + 16)};
      Quad(scallop, x(x0 + 9, ay + 16), x(x0 + 4.5f, ay + 21), x(x0, ay + 16));
      FillShape(c, scallop, i % 2 ? WithAlpha(kParchment, 0.75f) : WithAlpha(kCopper, 0.85f));
    }
  }
  // The fountain's basin (its water and jets light each frame).
  if (top < 3600.0f && bottom > 3560.0f) {
    art::FilledEllipse(c, x(city::kFountain), glm::vec2(63.0f, 17.0f) * s, kShelf);
    art::FilledEllipse(c, x(city::kFountain), glm::vec2(61.0f, 15.0f) * s, kWood);
  }
  // The shop front: cornice, planked wall between pilasters, the sign board, the bell's bracket, the sill and
  // step (the window, its fanlight and what's in them are drawn each frame).
  const city::Box& front = city::kShopFront;
  if (top > front.y1 || bottom < front.y0) return;
  RoundRect(c, {x(front.x0, front.y0), x(front.x1, front.y0 + 16)}, 0.0f, kWood, kCopper, 1.2f * s);
  c.FillRect({x(28, front.y0 + 16), x(362, 2952)}, kPlank);
  for (float px = 64.0f; px < 338.0f; px += 22.0f) c.Line(x(px, 2582), x(px, 2952), 1.0f * s, kPlankLine);
  RoundRect(c, {x(28, front.y0 + 16), x(52, 2952)}, 0.0f, kPlankLine, kWood, 1.0f * s);
  RoundRect(c, {x(338, front.y0 + 16), x(362, 2952)}, 0.0f, kPlankLine, kWood, 1.0f * s);
  const city::Box& sign = city::kShopSign;
  RoundRect(c, {x(sign.x0, sign.y0), x(sign.x1, sign.y1)}, 4.0f * s, Hex(0x2B1E14), kCopper, 2.0f * s);
  std::vector<glm::vec2> bracket;
  for (Vec2 q : city::Cubic{{362, 2588}, {372, 2588}, {376, 2596}, {368, 2600}}.Points(8)) bracket.push_back(x(q));
  bracket.push_back(x(352, 2600));
  art::Polyline(c, bracket, 2.5f * s, kCopper);
  c.FillRect({x(40, 2938), x(350, 2952)}, kShelf);
  c.FillRect({x(32, 2952), x(358, front.y1)}, kWood);
}

void CityArt::DrawMarket(Canvas& p, Canvas& c, const Xf& x, const CityFrame& frame) {
  const float s = x.s, t = frame.time, view_bottom = frame.scroll + frame.ViewHeight();
  const city::District& d = city::Districts()[1];
  if (frame.scroll > city::kMarketBottom + 80.0f || view_bottom < d.plate) return;
  const CityFrame::Market& m = frame.market;
  const Palette& palette = *frame.palette;
  auto visible = [&](float y0, float y1) { return y1 > frame.scroll - 30.0f && y0 < view_bottom + 30.0f; };
  // How full the window is: the glass warms, the light on the square and the fountain grow with it.
  float full = 0.0f;
  for (int i = 0; i < 15; ++i) full += Ease(Clamp01(m.bottle[static_cast<size_t>(i)][0]));
  full /= 15.0f;

  // The window's light spills onto the cobbles (the lower part of an ellipse under the sill).
  if (visible(2962.0f, 3102.0f)) {
    wlw::ui::ImageStyle style;
    style.tint = WithAlpha(kAmberLit, 0.8f * (0.05f + 0.55f * full));
    style.uv = {{0.0f, (2962.0f - 2882.0f) / 220.0f}, {1.0f, 1.0f}};
    p.Image({x(-65, 2962), x(455, 3102)}, soft_, style);
  }
  if (visible(city::kShopFront.y0, city::kShopFront.y1)) {
    // The sign lights as the shop wakes.
    const city::Box& sign = city::kShopSign;
    const glm::vec2 sign_mid = x((sign.x0 + sign.x1) * 0.5f, (sign.y0 + sign.y1) * 0.5f);
    if (m.awake > 0.0f) glow_.Draw(p, sign_mid, glm::vec2(150.0f, 34.0f) * s, WithAlpha(kAmberLit, 0.22f * m.awake));
    c.Text(*frame.font, "APOTHECARY", sign_mid, 19.0f * kText * s, Align::Center,
           {.color = Mixed(Hex(0x5A4430), kParchment, m.awake)});

    // The fanlight: dark glass under a copper arch, the muntins, the challenges' show globes.
    const Rgba glass = Mixed(Hex(0x120C08), Hex(0x3A2614), full);
    std::vector<glm::vec2> arch{x(60, 2666)};
    for (int k = 0; k <= 24; ++k) {
      const float a = kPi + kPi * static_cast<float>(k) / 24.0f;
      arch.push_back(x(195.0f + 135.0f * std::cos(a), 2642.0f + 52.0f * std::sin(a)));
    }
    arch.push_back(x(330, 2666));
    FillShape(p, arch, glass);
    for (Vec2 to : {Vec2{195, 2590}, Vec2{118, 2602}, Vec2{272, 2602}})
      p.Line(x(195, 2666), x(to), 2.0f * s, kCopperDark);
    for (int k = 0; k < city::kLobes; ++k) {
      const size_t i = static_cast<size_t>(15 + k);
      const glm::vec2 g = x(city::GlobeSpot(k));
      const float a = Clamp01(m.bottle[i][0]);
      const Rgba color = m.colors[i].empty() ? kLight : LiquidColor(palette, m.colors[i][0]);
      if (a > 0.0f) glow_.Draw(p, g, 30.0f * s, WithAlpha(color, 0.7f * a));
      p.Line(g - glm::vec2(0.0f, 14.0f * s), g - glm::vec2(0.0f, 24.0f * s), 2.0f * s, kShopBrass);
      p.Circle(g, 14.75f * s, kShopBrass);
      p.Circle(g, 13.25f * s, Mixed(Hex(0x1C130C), color, a));
      p.Circle(g + glm::vec2(-5.0f, -5.0f) * s, 3.5f * s, Hex(0xFFFFFF, 0.35f));
      if (m.bottle_flash[i] > 0.0f) p.Ring(g, 20.0f * s, 3.0f * s, Hex(0xFFFFFF, m.bottle_flash[i]));
    }
    Outline(p, arch, 3.0f * s, kCopper);

    // The window: one bottle per level on three shelves.
    const city::Box& w = city::kShopWindow;
    p.FillRect({x(w.x0, w.y0), x(w.x1, w.y1)}, glass);
    p.Line(x(78, 2678), x(120, 2678), 2.0f * s, Hex(0xFFFFFF, 0.06f));
    p.Line(x(86, 2686), x(104, 2686), 2.0f * s, Hex(0xFFFFFF, 0.06f));
    for (float y : city::kShelves) {
      p.FillRect({x(w.x0 + 2, y), x(w.x1 - 2, y + 6)}, kShelf);
      p.FillRect({x(w.x0 + 2, y + 6), x(w.x1 - 2, y + 9)}, kWood);
    }
    for (int i = 0; i < 15; ++i) DrawBottle(p, c, x, frame, i);
    Outline(p, {x(w.x0, w.y0), x(w.x1, w.y0), x(w.x1, w.y1), x(w.x0, w.y1)}, 4.0f * s, kCopper);

    // The door bell swings when it rings.
    const float swing = m.bell >= 0.0f ? city::BellSwing(m.bell) * kPi / 180.0f : 0.0f;
    const glm::vec2 pivot = x(city::kBellPivot);
    auto bell = [&](float bx, float by) {
      const float cs = std::cos(swing), sn = std::sin(swing);
      return pivot + glm::vec2(bx * cs - by * sn, bx * sn + by * cs) * s;
    };
    std::vector<glm::vec2> dome{bell(-9, 24)};
    Quad(dome, bell(-9, 24), bell(-9, 8), bell(0, 6));
    Quad(dome, bell(0, 6), bell(9, 8), bell(9, 24));
    p.Line(bell(0, 0), bell(0, 6), 2.0f * s, kCopper);
    FillShape(p, dome, kShopBrass);
    FillShape(p, {bell(-9, 23.5f), bell(9, 23.5f), bell(12, 28), bell(-12, 28)}, Hex(0xA87A3E));
    p.Line(bell(-4, 11), bell(-6, 22), 1.5f * s, Hex(0xF0C27A, 0.8f));
    p.Circle(bell(0, 31), 3.0f * s, kShopBrass);
    if (m.bell >= 0.0f && m.bell < 1.5f)
      glow_.Draw(p, bell(0, 18), 22.0f * s, Hex(0xFFC27A, 0.8f * (1.0f - m.bell / 1.5f)));
  }

  // The trail between the lamps: each piece lights once the lamp it leaves from has.
  for (const city::LampTrail& piece : city::MarketTrail()) {
    if (!visible(piece.curve.a.y - 40.0f, piece.curve.d.y + 40.0f)) continue;
    std::vector<glm::vec2> points;
    for (Vec2 q : piece.curve.Points(12)) points.push_back(x(q));
    const bool lit = m.lamp[static_cast<size_t>(piece.from)][0] > 0.0f;
    Dots(p, points, 11.0f * s, 3.0f * s, 1.6f * s, lit ? kAmberLit : Hex(0x4A3424));
  }

  // The fountain: its water and jets glow with the window.
  const float fountain = 0.1f + 0.8f * full;
  if (visible(3490.0f, 3600.0f)) {
    glow_.Draw(p, x(195, 3562), 70.0f * s, WithAlpha(kAmberLit, 0.8f * fountain));
    art::FilledEllipse(p, x(195, 3576), glm::vec2(52.0f, 11.0f) * s, WithAlpha(kAmberLit, fountain));
    RoundRect(p, {x(189, 3534), x(201, 3576)}, 0.0f, kPlank, kShelf, 1.5f * s);
    const float flicker = frame.calm ? 0.85f : 0.78f + 0.14f * std::sin(t * 1.9f) + 0.08f * std::sin(t * 4.7f);
    for (float side : {-1.0f, 1.0f}) {
      std::vector<glm::vec2> jet;
      const city::Cubic curve{{195, 3534}, {195 + 10 * side, 3518}, {195 + 19 * side, 3524}, {195 + 23 * side, 3552}};
      for (Vec2 q : curve.Points(10)) jet.push_back(x(q));
      art::Polyline(p, jet, 3.0f * s, Hex(0xFFD08A, fountain * flicker));
    }
  }

  DrawMarketLamps(p, c, x, frame);

  // Asleep behind its gate: the market lies under a thick mist that lifts as the shop wakes.
  if (m.awake < 1.0f) p.FillRect({x(0, d.top), x(kWidth, d.bottom)}, Hex(0x0B111D, 0.62f * (1.0f - m.awake)));
}

void CityArt::DrawBottle(Canvas& p, Canvas& c, const Xf& x, const CityFrame& frame, int level) {
  const float s = x.s;
  const size_t i = static_cast<size_t>(level);
  const Vec2 b = city::BottleSpot(level);
  auto at = [&](float dx, float dy) { return x(b.x + dx, b.y + dy); };
  const auto& state = frame.market.bottle[i];
  const float fill = Ease(Clamp01(state[0])), cork = Ease(Clamp01(state[1])), label = Ease(Clamp01(state[2]));
  const float gold = Clamp01(state[3]);
  const std::vector<Color>& colors = frame.market.colors[i];
  const Palette& palette = *frame.palette;
  // The body up to the neck (convex: the liquid is cut from it), then the outline with the neck and lip.
  std::vector<glm::vec2> body{at(-15, -4)};
  Quad(body, at(-15, -4), at(-15, 0), at(-11, 0));
  body.push_back(at(11, 0));
  Quad(body, at(11, 0), at(15, 0), at(15, -4));
  body.push_back(at(15, -36));
  Quad(body, at(15, -36), at(15, -45), at(5.5f, -47));
  std::vector<glm::vec2> edge = body;
  for (glm::vec2 q : {at(5.5f, -56), at(6.5f, -56), at(6.5f, -60), at(-6.5f, -60), at(-6.5f, -56), at(-5.5f, -56)})
    edge.push_back(q);
  body.push_back(at(-5.5f, -47));
  edge.push_back(at(-5.5f, -47));
  Quad(body, at(-5.5f, -47), at(-15, -45), at(-15, -36));
  Quad(edge, at(-5.5f, -47), at(-15, -45), at(-15, -36));

  if (fill > 0.0f && !colors.empty())
    glow_.Draw(p, at(0, -26), 26.0f * s, WithAlpha(LiquidColor(palette, colors[0]), 0.35f * fill));
  FillShape(p, body, Hex(0xFFFFFF, 0.04f));
  p.FillRect({at(-5.5f, -56), at(5.5f, -47)}, Hex(0xFFFFFF, 0.04f));
  // The level's colours in bands from the bottom up.
  if (fill > 0.0f && !colors.empty()) {
    const float each = 47.0f * fill / static_cast<float>(colors.size());
    for (size_t k = 0; k < colors.size(); ++k) {
      const float y0 = at(0, -each * static_cast<float>(k + 1)).y - 0.3f * s;
      const float y1 = at(0, -each * static_cast<float>(k)).y + 0.3f * s;
      const std::vector<glm::vec2> band = ClipSlab(body, y0, y1, 1);
      if (band.size() >= 3) FillShape(p, band, LiquidColor(palette, colors[k]));
    }
  }
  p.FillRect({at(-11, -40), at(-8, -10)}, Hex(0xFFFFFF, 0.25f));
  Outline(p, edge, 1.4f * s, fill > 0.0f ? Hex(0xE8C9A0) : Hex(0x6E5038));
  // Lean corks it (the cork drops in), Swift labels it (the label slides on), all three seal it in gold.
  if (cork > 0.0f) {
    const float y = -66.0f - 18.0f * (1.0f - cork);
    RoundRect(p, {at(-5, y), at(5, y + 9)}, 2.0f * s, Hex(0xB98A55, cork), Hex(0x6E4A1E, cork), 0.8f * s);
  }
  if (gold > 0.0f) {
    const std::vector<glm::vec2> foil{at(-6.5f, -67), at(6.5f, -67), at(7.5f, -54), at(-7.5f, -54)};
    FillShape(p, foil, Hex(0xF0C060, gold));
    Outline(p, foil, 0.8f * s, Hex(0x9A6A2E, gold));
  }
  if (label > 0.0f) {
    const float dx = -14.0f * (1.0f - label);
    RoundRect(c, {at(dx - 11, -30), at(dx + 11, -17)}, 1.5f * s, WithAlpha(kParchment, label),
              WithAlpha(gold > 0.0f ? Hex(0xE0B040) : kShelf, label), 1.0f * s);
    if (frame.hand)
      c.Text(*frame.hand, std::to_string(level + 1), at(dx, -23.5f), 10.0f * kText * s, Align::Center,
             {.color = Hex(0x3A2A1A, label)});
  }
  if (frame.market.bottle_flash[i] > 0.0f)
    p.Ring(at(0, -26), 22.0f * s, 3.0f * s, Hex(0xFFFFFF, frame.market.bottle_flash[i]));
}

void CityArt::DrawMarketLamps(Canvas& p, Canvas& c, const Xf& x, const CityFrame& frame) {
  const float s = x.s, t = frame.time, view_bottom = frame.scroll + frame.ViewHeight();
  const CityFrame::Market& m = frame.market;
  const bool shut = !frame.districts[0].open;
  const wlw::ui::Font& font = *frame.font;
  const auto lamps = city::DistrictLamps(2);
  for (int j = 0; j < city::kDistrictLamps; ++j) {
    const size_t n = static_cast<size_t>(j);
    const Vec2 l = lamps[n];
    if (l.y + 70.0f < frame.scroll || l.y - 70.0f > view_bottom) continue;
    const bool diamond = j >= 15, locked = m.locked[n];
    const auto& a = m.lamp[n];
    const bool perfect = a[0] >= 1.0f && a[1] >= 1.0f && a[2] >= 1.0f;
    const Rgba ring = locked ? Hex(0x3A2A1C) : perfect ? Hex(0xFFE6A6) : a[0] > 0.0f ? kCopper : kShelf;
    const glm::vec2 at = x(l);
    if (a[0] > 0.0f) glow_.Draw(p, at, 40.0f * s, Hex(0xFFC27A, 0.5f * Clamp01(a[0])));
    const float cap = diamond ? 30.0f : 27.0f;
    RoundRect(p, {x(l.x - 7, l.y - cap), x(l.x + 7, l.y - cap + 7)}, 2.0f * s, kPlank, ring, 1.2f * s);
    const std::vector<glm::vec2> body = diamond ? DiamondPoints(at, 22.0f * s) : CirclePoints(at, 20.0f * s);
    FillShape(p, body, locked ? Hex(0x150F0A) : Hex(0x24180F));
    // Lit: the level's colours, a sliver per cup.
    const std::vector<Color>& colors = m.colors[n];
    if (a[0] > 0.0f && !colors.empty()) {
      const float count = static_cast<float>(colors.size());
      for (size_t k = 0; k < colors.size(); ++k) {
        const float x0 = at.x + (-22.0f + 44.0f * static_cast<float>(k) / count) * s;
        const float x1 = at.x + (-22.0f + 44.0f * static_cast<float>(k + 1) / count) * s;
        const std::vector<glm::vec2> sliver = ClipSlab(body, x0, x1);
        if (sliver.size() >= 3) FillShape(p, sliver, WithAlpha(LiquidColor(*frame.palette, colors[k]), Clamp01(a[0])));
      }
    }
    Outline(p, body, (perfect ? 3.5f : 2.5f) * s, ring);
    if (m.lamp_flash[n][0] > 0.0f) Outline(p, body, 5.0f * s, Hex(0xFFFFFF, m.lamp_flash[n][0]));

    // The number (none while the gate is shut), and its stars under it (not while locked).
    if (shut) continue;
    const std::string number = diamond ? "C" + std::to_string(j - 14) : std::to_string(j + 1);
    const float size = (diamond ? 15.0f : 19.0f) * kText * s;
    if (locked) {
      c.Text(font, number, at, size, Align::Center, {.color = Hex(0xF3E8CC, 0.22f)});
    } else if (a[0] > 0.0f) {
      c.Text(font, number, at, size, Align::Center, Outlined(Hex(0xFFFFFF)));
    } else {
      c.Text(font, number, at, size, Align::Center, {.color = kParchment});
    }
    if (locked) continue;
    for (int k = 0; k < 3; ++k) {
      const glm::vec2 star = x(l.x + static_cast<float>(k - 1) * city::kStarSpacing, l.y + (diamond ? 38.0f : 34.0f));
      StarShape(c, star, (city::kStarRadius + 1.0f) * s, Hex(0x6E5038));
      StarShape(c, star, city::kStarRadius * s, Hex(0x3A281A));
      const float lit = Clamp01(a[static_cast<size_t>(k)]);
      if (lit > 0.0f) {
        const float pop = lit < 1.0f ? 1.0f + 0.7f * std::sin(lit * kPi) : 1.0f;
        StarShape(c, star, (city::kStarRadius + 1.0f) * pop * s, Hex(0xFFE08A, lit));
        StarShape(c, star, city::kStarRadius * pop * s, Hex(0xFFC53D, lit));
      }
    }
  }
  // The next level to play breathes.
  if (m.breathe >= 0 && m.breathe < city::kDistrictLamps) {
    const glm::vec2 at = x(lamps[static_cast<size_t>(m.breathe)]);
    const float period = m.pulse ? 0.7f : 2.4f;
    const float o = frame.calm ? 0.8f : 0.6f - 0.4f * std::cos(t * 2.0f * kPi / period);
    const std::vector<glm::vec2> ring =
        m.breathe >= 15 ? DiamondPoints(at, 28.0f * s) : CirclePoints(at, 26.0f * s, 36);
    Outline(p, ring, 3.0f * s, Hex(0xFFE6A6, o));
  }
}

// ---- The canal quarter (World 3) ----

void CityArt::DrawQuarterStill(Canvas& c, const Xf& x, float top, float bottom) {
  using namespace canal::color;
  const city::District& d = city::Districts()[2];
  if (bottom < city::kMarketBottom || top > d.bottom) return;
  const float s = x.s;
  c.FillRect({x(0, d.top), x(kWidth, d.bottom)}, kQuarterNight);
  // Rooftops seen from above: the half away from the moon in shade, a ridge, a chimney (skylights light each frame).
  for (const city::QuarterRoof& q : city::QuarterRoofs()) {
    const canal::Roof& roof = q.roof;
    if (roof.box.max.y < top || roof.box.min.y > bottom) continue;
    const Rect r{x(roof.box.min), x(roof.box.max)};
    c.FillRect(r, kRoofTones[roof.tone]);
    const glm::vec2 mid = (r.min + r.max) * 0.5f;
    if (roof.across) {
      c.FillRect({{r.min.x, mid.y}, r.max}, kRoofShade);
      c.Line({r.min.x + 2.0f * s, mid.y}, {r.max.x - 2.0f * s, mid.y}, 1.0f * s, kRidge);
    } else {
      c.FillRect({{mid.x, r.min.y}, r.max}, kRoofShade);
      c.Line({mid.x, r.min.y + 2.0f * s}, {mid.x, r.max.y - 2.0f * s}, 1.0f * s, kRidge);
    }
    const glm::vec2 chimney = x(roof.chimney);
    c.FillRect({chimney, chimney + glm::vec2(3.0f * s)}, kChimney);
  }
  for (const canal::Tree& tree : city::QuarterTrees()) {
    const glm::vec2 at = x(tree.at);
    c.Circle(at, tree.r * s, kTreeDark);
    c.Circle(at - glm::vec2(tree.r * 0.3f * s), tree.r * 0.55f * s, kTreeLight);
  }
  if (top > city::kLagoon.y1 + 20.0f || bottom < city::kSluice.y0 - 20.0f) return;
  // The Grand Canal: bank, quay, water, the lagoon at its end, the sluice under the plate, its bridges.
  std::vector<glm::vec2> points;
  for (const glm::vec2 q : grand_canal_.points) points.push_back(x(q.x, q.y));
  const art::Path canal = art::MakePath(std::move(points));
  for (const auto& [width, color] : {std::pair{34.0f, kBank}, {29.0f, kQuay}, {25.0f, kWater}})
    art::Stroke(c, canal, 0.0f, width * s, color);
  const city::Box& lagoon = city::kLagoon;
  c.FillRect({x(lagoon.x0, lagoon.y0), x(lagoon.x1, lagoon.y1)}, kBank, city::kLagoonCorner * s);
  c.FillRect({x(lagoon.x0 + 5, lagoon.y0 + 5), x(lagoon.x1 - 5, lagoon.y1 - 5)}, kQuay, (city::kLagoonCorner - 4) * s);
  c.FillRect({x(lagoon.x0 + 8, lagoon.y0 + 8), x(lagoon.x1 - 8, lagoon.y1 - 8)}, kWater, (city::kLagoonCorner - 6) * s);
  const city::Box& sluice = city::kSluice;
  c.FillRect({x(sluice.x0, sluice.y0), x(sluice.x1, sluice.y1)}, kBank, 3.0f * s);
  art::Polyline(c, {x(183, sluice.y1 - 2), x(195, sluice.y1 - 8), x(207, sluice.y1 - 2)}, 2.4f * s, kBrass);
  for (const city::Box& b : city::QuarterBridges())
    RoundRect(c, {x(b.x0, b.y0), x(b.x1, b.y1)}, 0.0f, Hex(0x3A4560), kBollard, 1.0f * s);
  for (const Vec2 lamp : city::QuayLamps()) {
    if (lamp.y < top - 10.0f || lamp.y > bottom + 10.0f) continue;
    glow_.Draw(c, x(lamp), 5.0f * s, kLampGlow);
    c.Circle(x(lamp), 1.4f * s, WithAlpha(kLampLight, 0.7f));
  }
}

void CityArt::DrawQuarter(Canvas& p, Canvas& c, const Xf& x, const CityFrame& frame) {
  using namespace canal::color;
  const city::District& d = city::Districts()[2];
  const float s = x.s, t = frame.time, view_top = frame.scroll, view_bottom = frame.scroll + frame.ViewHeight();
  if (view_top > d.bottom || view_bottom < city::kMarketBottom) return;
  const CityFrame::Quarter& q = frame.quarter;

  // Each neighbourhood lights out from its harbour, roof by roof (the farthest at its reach); the skylights glow.
  std::array<float, 15> radius{};
  for (int h = 0; h < 15; ++h) {
    const float k = Clamp01(q.hood[static_cast<size_t>(h)]);
    radius[static_cast<size_t>(h)] =
        k > 0.0f ? 20.0f + (city::HoodReach(h) + 20.0f) * (1.0f - (1.0f - k) * (1.0f - k)) : -1.0f;
  }
  for (const city::QuarterRoof& roof : city::QuarterRoofs()) {
    const canal::Box& box = roof.roof.box;
    if (box.max.y < view_top - 10.0f || box.min.y > view_bottom + 10.0f) continue;
    const float lit = roof.hood >= 0 ? Clamp01((radius[static_cast<size_t>(roof.hood)] - roof.distance) / 40.0f) : 0.0f;
    if (lit > 0.0f) p.FillRect({x(box.min), x(box.max)}, WithAlpha(kHoodWarm, 0.12f * lit));
    for (const Vec2 window : roof.roof.windows) {
      const glm::vec2 w = x(window);
      if (lit > 0.0f) glow_.Draw(p, w + glm::vec2(1.0f * s), 7.0f * s, WithAlpha(kLampGlowLit, 0.9f * lit));
      p.FillRect({w, w + glm::vec2(2.0f * s)},
                 lit > 0.0f ? WithAlpha(kLampLit, 0.3f + 0.7f * lit) : Hex(0xFFD58A, 0.3f));
    }
  }

  // The water's ripples drift down the canal.
  const float drift = frame.calm ? 0.0f : t * 9.3f;
  Dashes(p, x, s, grand_canal_, 3.0f, 14.0f, drift, 1.2f, WithAlpha(kFlow, 0.7f), view_top, view_bottom,
         {-5.0f, -4.0f});

  // The light down the canal to the next harbour, barges of both colours moving down it.
  if (q.trail > 1.0f) {
    std::vector<glm::vec2> map_points, points;
    for (const Vec2 v : city::CanalPart(q.trail, 2.5f)) {
      map_points.push_back({v.x, v.y});
      points.push_back(x(v));
    }
    const art::Path lit = art::MakePath(points);
    art::Stroke(p, lit, 0.0f, 22.0f * s, WithAlpha(kTeal, 0.14f));
    art::Stroke(p, lit, 0.0f, 20.0f * s, WithAlpha(kCanalLit, 0.6f));
    const art::Path along = art::MakePath(std::move(map_points));
    const float run = frame.calm ? 0.0f : t * kBargeSpeed;
    Dashes(p, x, s, along, 9.0f, 40.0f, run, 6.0f, kBargeAmber, view_top, view_bottom);
    Dashes(p, x, s, along, 9.0f, 40.0f, run + 20.0f, 6.0f, kBargeBlue, view_top, view_bottom);
    if (q.running) glow_.Draw(p, points.back(), 16.0f * s, WithAlpha(kTealLit, 0.8f));
  }

  DrawQuarterLamps(p, c, x, frame);

  // Asleep behind its gate: the quarter lies dark under the mist, which lifts as it wakes.
  if (q.awake < 1.0f)
    p.FillRect({x(0, city::kMarketBottom), x(kWidth, d.bottom)}, Hex(0x0B111D, 0.62f * (1.0f - q.awake)));
}

void CityArt::DrawQuarterLamps(Canvas& p, Canvas& c, const Xf& x, const CityFrame& frame) {
  using namespace canal::color;
  const float s = x.s, t = frame.time, view_bottom = frame.scroll + frame.ViewHeight();
  const CityFrame::Quarter& q = frame.quarter;
  const bool shut = !frame.districts[1].open;
  const wlw::ui::Font& font = *frame.font;
  const auto lamps = city::DistrictLamps(3);
  for (int j = 0; j < city::kDistrictLamps; ++j) {
    const size_t n = static_cast<size_t>(j);
    const Vec2 l = lamps[n];
    if (l.y + 70.0f < frame.scroll || l.y - 70.0f > view_bottom) continue;
    const bool buoy = j >= 15, locked = q.locked[n];
    const auto& a = q.lamp[n];
    const bool perfect = a[0] >= 1.0f && a[1] >= 1.0f && a[2] >= 1.0f;
    const glm::vec2 at = x(l);
    // A harbour lit by its neighbourhood (a buoy by its own light): the warm halo, the basin, the level's colours
    // on the water, the ring.
    const float halo = buoy ? Clamp01(a[0]) : Clamp01(q.hood[n]);
    if (halo > 0.0f) glow_.Draw(p, at, 60.0f * s, WithAlpha(kHoodWarm, 0.4f * halo));
    const std::vector<glm::vec2> basin = buoy ? DiamondPoints(at, 30.0f * s) : CirclePoints(at, 27.0f * s, 32);
    FillShape(p, basin, kWater);
    Outline(p, basin, 5.0f * s, kQuay);
    const std::vector<glm::vec2> inner = buoy ? DiamondPoints(at, 19.0f * s) : CirclePoints(at, 17.5f * s, 32);
    FillShape(p, inner, locked ? kBasinLocked : kBasinWater);
    const std::vector<Color>& colors = q.colors[n];
    if (a[0] > 0.0f && !colors.empty()) {
      const float count = static_cast<float>(colors.size());
      for (size_t k = 0; k < colors.size(); ++k) {
        const float x0 = at.x + (-18.0f + 36.0f * static_cast<float>(k) / count) * s;
        const float x1 = at.x + (-18.0f + 36.0f * static_cast<float>(k + 1) / count) * s;
        const std::vector<glm::vec2> sliver = ClipSlab(inner, x0, x1);
        if (sliver.size() >= 3)
          FillShape(p, sliver, WithAlpha(LiquidColor(*frame.palette, colors[k]), 0.85f * Clamp01(a[0])));
      }
    }
    const bool next = j == q.breathe;
    const std::vector<glm::vec2> ring = buoy ? DiamondPoints(at, 22.0f * s) : CirclePoints(at, 20.0f * s, 32);
    const Rgba ring_color = perfect ? kHarbourPerfect : locked ? kHarbourLocked : next ? kTealLit : kHarbourRing;
    Outline(p, ring, (next ? 4.0f : 3.0f) * s, ring_color);
    if (q.lamp_flash[n][0] > 0.0f) Outline(p, ring, 5.0f * s, Hex(0xFFFFFF, q.lamp_flash[n][0]));

    // The number (none while the gate is shut, faint while locked), and its stars under it (not while locked).
    if (shut) continue;
    const std::string number = buoy ? "C" + std::to_string(j - 14) : std::to_string(j + 1);
    const float size = (buoy ? 13.0f : 17.0f) * kText * s;
    wlw::ui::TextStyle style{.color = kPlateInk, .outline = 0.1f, .outline_color = Hex(0x0A101B, 0.9f)};
    if (locked) {
      style = {.color = Hex(0xEEF4FF, 0.22f)};
    } else if (a[0] > 0.0f) {
      style = {
          .color = kQuarterNight, .outline = 0.1f, .outline_color = Rgba{1.0f, 240.0f / 255.0f, 210.0f / 255.0f, 0.7f}};
    }
    c.Text(font, number, at, size, Align::Center, style);
    if (locked) continue;
    for (int k = 0; k < 3; ++k) {
      const glm::vec2 star = x(l.x + static_cast<float>(k - 1) * 13.0f, l.y + (buoy ? 40.0f : 38.0f));
      const float lit = Clamp01(a[static_cast<size_t>(k)]);
      const float r = lit > 0.0f ? 5.0f + 2.5f * (1.0f - lit) : 5.0f;
      StarShape(c, star, (r + 1.0f) * s, lit > 0.0f ? Hex(0xB5781C, std::max(0.4f, lit)) : Hex(0x3B4A6C, 0.9f));
      StarShape(c, star, r * s, lit > 0.0f ? Hex(0xFFC24D, std::max(0.4f, lit)) : Hex(0x1A2236, 0.9f));
    }
  }
  // The next level to play breathes: a teal ring round its basin.
  if (q.breathe >= 0 && q.breathe < city::kDistrictLamps) {
    const glm::vec2 at = x(lamps[static_cast<size_t>(q.breathe)]);
    const float period = q.pulse ? 0.7f : 2.4f, low = q.pulse ? 0.15f : 0.2f;
    const float o = frame.calm ? 0.8f : low + (1.0f - low) * (0.5f - 0.5f * std::cos(t * 2.0f * kPi / period));
    const std::vector<glm::vec2> ring =
        q.breathe >= 15 ? DiamondPoints(at, 30.0f * s) : CirclePoints(at, 27.0f * s, 36);
    Outline(p, ring, 3.0f * s, WithAlpha(kTealLit, o));
  }
}

// ---- The sanctum floor (World 4) ----

void CityArt::DrawSanctumStill(Canvas& c, const Xf& x, float top, float bottom) {
  const city::District& d = city::Districts()[3];
  if (bottom < d.top || top > city::kHeight) return;
  const float s = x.s;
  const Vec2 centre = city::kSanctumCentre;
  const glm::vec2 C = x(centre);
  auto shows = [&](float y0, float y1) { return y1 > top - 10.0f && y0 < bottom + 10.0f; };
  // The floor, a faint glow round the sigil (cut off above the district).
  c.FillRect({x(0, d.top), x(kWidth, city::kHeight)}, kSanctumFloor);
  wlw::ui::ImageStyle floor_glow;
  floor_glow.tint = kFloorGlow;
  floor_glow.uv = {{0.0f, (d.top - (centre.y - 560.0f)) / 1120.0f}, {1.0f, 1.0f}};
  c.Image({x(centre.x - 560.0f, d.top), x(centre.x + 560.0f, centre.y + 560.0f)}, soft_, floor_glow);
  // The slabs' joints: rings round the sigil and spokes out from it.
  for (float r = 200.0f; r < 560.0f; r += 46.0f) c.Ring(C, r * s, 1.0f * s, kSlab);
  for (int j = 0; j < 24; ++j) {
    const float b = static_cast<float>(j) * 15.0f * kPi / 180.0f;
    const glm::vec2 dir{std::cos(b), std::sin(b)}, middle{centre.x, centre.y};
    const glm::vec2 a = middle + dir * 200.0f;
    glm::vec2 e = middle + dir * 620.0f;
    if (a.y < d.top) continue;
    if (e.y < d.top) e = a + (e - a) * ((d.top - a.y) / (e.y - a.y));
    c.Line(x(a.x, a.y), x(e.x, e.y), 1.0f * s, kSlab);
  }
  // The stair down from the plate, the pillars, the groove down the stair.
  if (shows(city::kSanctumPlate.y1, 5800.0f)) {
    for (int k = 0; k < city::kSanctumSteps; ++k) {
      const city::Box b = city::SanctumStep(k);
      c.FillRect({x(b.x0, b.y0), x(b.x1, b.y1)}, k % 2 ? Hex(0x17121F) : Hex(0x1C1626));
      c.Line(x(b.x0, b.y0), x(b.x1, b.y0), 1.0f * s, WithAlpha(kSigilGold, 0.35f));
    }
    for (const Vec2 pillar : city::kSanctumPillars) {
      wlw::ui::ImageStyle shadow;
      shadow.tint = Hex(0x000000, 0.45f);
      const glm::vec2 at = x(pillar);
      c.Image({at - glm::vec2(26.0f * s), at + glm::vec2(26.0f * s)}, soft_, shadow);
      c.Circle(at, 19.0f * s, kHallEdge);
      c.Circle(at, 17.0f * s, Hex(0x1E1828));
      c.Ring(at, 12.0f * s, 1.0f * s, WithAlpha(kSigilGold, 0.3f));
    }
    const glm::vec2 a = x(195, city::kSanctumGrooveTop), b = x(195, city::kSanctumGrooveBottom);
    c.Line(a, b, 16.0f * s, Hex(0xB49450, 0.35f));
    c.Line(a, b, 10.0f * s, Hex(0x040307));
  }
  // The sigil inlaid in the floor: its gold rings, the runes between them, the dashed and inner rings, the guide of
  // the star the strokes will draw.
  if (shows(centre.y - 200.0f, centre.y + 200.0f)) {
    c.Circle(C, 200.0f * s, WithAlpha(kSigilGold, 0.04f));
    c.Ring(C, city::kSigilRing * s, 2.6f * s, kSigilGold);
    c.Ring(C, 166.0f * s, 1.0f * s, kSigilGold);
    for (const auto& rune : runes_) c.Line(x(rune[0]), x(rune[1]), 1.2f * s, WithAlpha(kSigilGold, 0.7f));
    DashedRing(c, C, 104.0f * s, 4.0f * s, 10.0f * s, 0.9f * s, WithAlpha(kSigilGold, 0.5f));
    c.Ring(C, 40.0f * s, 1.4f * s, WithAlpha(kSigilGold, 0.8f));
    for (int i = 0; i < 15; ++i) {
      const auto ends = city::StrokeEnds(i);
      DashedLine(c, x(ends[0]), x(ends[1]), 3.0f * s, 8.0f * s, 0.8f * s, WithAlpha(kSigilGold, 0.12f));
    }
  }
  // The plaque at the floor's foot.
  const city::Box& plaque = city::kSanctumPlaque;
  if (shows(plaque.y0 - 10.0f, plaque.y1)) {
    c.Line(x(120, plaque.y0 - 1), x(270, plaque.y0 - 1), 1.0f * s, WithAlpha(kSigilGold, 0.4f));
    RoundRect(c, {x(plaque.x0, plaque.y0), x(plaque.x1, plaque.y1)}, 6.0f * s, Hex(0x140F1E), kHallEdge, 2.0f * s);
    const float rows[][2] = {{5791.0f, 251.0f}, {5803.0f, 239.0f}, {5815.0f, 245.0f}};
    for (const auto& row : rows) c.Line(x(139, row[0]), x(row[1], row[0]), 1.2f * s, Hex(0xD8C49A, 0.18f));
  }
}

void CityArt::DrawSanctum(Canvas& p, Canvas& c, const Xf& x, const CityFrame& frame) {
  const city::District& d = city::Districts()[3];
  const float s = x.s, t = frame.time, view_top = frame.scroll, view_bottom = frame.scroll + frame.ViewHeight();
  if (view_bottom < d.top - 40.0f || view_top > city::kHeight) return;
  const CityFrame::Sanctum& S = frame.sanctum;
  const Vec2 centre = city::kSanctumCentre;
  const glm::vec2 C = x(centre);
  float level = 0.0f;
  for (const float stroke : S.stroke) level += Ease(Clamp01(stroke)) / 15.0f;
  const float all = Clamp01(S.all);

  // The elixir's green down the groove from the stair (once the gate is open), a glow at its head while it runs.
  if (S.pour > 0.0f) {
    const float head = city::kSanctumGrooveTop + (city::kSanctumGrooveBottom - city::kSanctumGrooveTop) * Ease(S.pour);
    const art::Path path = S.pour >= 1.0f ? groove_ : groove_.Part(0.0f, head - city::kSanctumGrooveTop);
    Dashes(p, x, s, path, 8.0f, 20.0f, frame.calm ? 0.0f : t * 25.0f, 4.0f, WithAlpha(kElixir, 0.85f), view_top,
           view_bottom);
    if (S.running) glow_.Draw(p, x(195, head), 16.0f * s, WithAlpha(kElixirLight, 0.8f));
  }
  if (view_bottom > centre.y - 200.0f && view_top < centre.y + 200.0f) {
    // The haze over the sigil grows with the star; the ring lights round once it's closed.
    const float extra = 0.08f * level + 0.06f * all;
    if (extra > 0.0f) p.Circle(C, 200.0f * s, WithAlpha(kSigilGold, extra));
    if (all > 0.0f) {
      p.Arc(C, city::kSigilRing * s, 7.0f * s, 0.0f, 2.0f * kPi * Ease(all), WithAlpha(kStrokeLit, 0.25f));
      p.Arc(C, city::kSigilRing * s, 3.0f * s, 0.0f, 2.0f * kPi * Ease(all), WithAlpha(kStrokeLit, 0.9f));
    }
    // The strokes of the star, each from its candle (thicker for a level with all three stars).
    for (int i = 0; i < 15; ++i) {
      const size_t n = static_cast<size_t>(i);
      if (S.stroke[n] <= 0.0f) continue;
      const auto ends = city::StrokeEnds(i);
      const float u = Ease(Clamp01(S.stroke[n]));
      const Vec2 e{ends[0].x + (ends[1].x - ends[0].x) * u, ends[0].y + (ends[1].y - ends[0].y) * u};
      const auto& a = S.lamp[n];
      const bool perfect = a[0] >= 1.0f && a[1] >= 1.0f && a[2] >= 1.0f;
      const float w = perfect ? 2.6f : 2.0f, op = perfect ? 0.95f : 0.79f;
      p.Line(x(ends[0]), x(e), (w + 4.0f) * s, WithAlpha(kStrokeLit, 0.22f * op));
      p.Line(x(ends[0]), x(e), w * s, WithAlpha(kStrokeLit, op));
      if (u < 1.0f) glow_.Draw(p, x(e), 12.0f * s, Hex(0xFFF2C8, 0.9f));
    }
    // The challenges' rings at the heart.
    const bool shut = !frame.districts[2].open;
    for (int k = 0; k < 3; ++k) {
      const Vec2 ring_at = city::SanctumRing(k);
      const glm::vec2 R = x(ring_at);
      const size_t n = static_cast<size_t>(15 + k);
      const float on = Clamp01(S.ring[static_cast<size_t>(k)]);
      const bool locked = S.locked[n];
      if (on > 0.0f) glow_.Draw(p, R, 30.0f * s, WithAlpha(kStrokeLit, 0.55f * on));
      const Rgba col = on > 0.0f ? kStrokeLit : locked ? kHallEdge : kSigilGold;
      p.Circle(R, 16.0f * s, kHallDark);
      p.Ring(R, 16.0f * s, (on > 0.0f ? 2.6f : 1.6f) * s, col);
      DashedRing(p, R, 11.0f * s, 2.0f * s, 4.4f * s, 1.0f * s, WithAlpha(col, 0.8f));
      art::FillConvex(p, DiamondPoints(R, 6.0f * s),
                      S.lamp[n][0] > 0.0f ? kStrokeLit
                      : locked            ? Hex(0x2A2236)
                                          : Hex(0x6E5E8A));
      if (shut || locked) continue;
      c.Text(*frame.font, "C" + std::to_string(k + 1), x(ring_at.x, ring_at.y + 23.0f), 9.0f * kText * s, Align::Center,
             {.color = Hex(0xC2B6E2)});
      const float angle = (-90.0f + 120.0f * static_cast<float>(k)) * kPi / 180.0f;
      const Vec2 mid{centre.x + std::cos(angle) * 93.0f, centre.y + std::sin(angle) * 93.0f};
      for (int j = 0; j < 3; ++j) {
        const float along = 9.0f * static_cast<float>(j - 1);
        const glm::vec2 star = x(mid.x - std::sin(angle) * along, mid.y + std::cos(angle) * along);
        const float lit = Clamp01(S.lamp[n][static_cast<size_t>(j)]);
        const float r = lit > 0.0f ? 3.5f + 2.0f * (1.0f - lit) : 3.5f;
        StarShape(c, star, (r + 1.0f) * s, lit > 0.0f ? Hex(0xB5781C, std::max(0.4f, lit)) : Hex(0x4A3E5E, 0.9f));
        StarShape(c, star, r * s, lit > 0.0f ? Hex(0xFFC24D, std::max(0.4f, lit)) : Hex(0x1E1828, 0.9f));
      }
    }
    // The elixir's phial at the centre: a halo, the beam once the star is closed, the glass, the liquid rising.
    glow_.Draw(p, C, (34.0f + 30.0f * level + 20.0f * all) * s, WithAlpha(kElixir, 0.06f + 0.25f * level + 0.2f * all));
    if (all > 0.0f) {
      const glm::vec2 beam_top = x(centre.x, centre.y - 160.0f * Ease(all));
      p.Line(C, beam_top, 11.0f * s, WithAlpha(kElixirLight, 0.3f * S.beam));
      p.Line(C, beam_top, 7.0f * s, WithAlpha(kElixirLight, S.beam));
    }
    p.FillRect({x(190, 5492), x(200, 5512)}, Hex(0xE8DEFF, 0.07f), 2.0f * s);
    Outline(p, {x(190, 5492), x(200, 5492), x(200, 5512), x(190, 5512)}, 1.4f * s, Hex(0xEDE4FF, 0.6f));
    p.FillRect({x(city::kPhialNeck.x0, city::kPhialNeck.y0), x(city::kPhialNeck.x1, city::kPhialNeck.y0 + 4.5f)},
               Hex(0xB49450), 2.0f * s);
    const float r = city::kPhialRadius;
    p.Circle(C, r * s, Hex(0xE8DEFF, 0.06f));
    if (level > 0.0f) {
      const float surface = city::ElixirTop(level);
      const std::vector<glm::vec2> band = ClipSlab(CirclePoints(C, r * s, 32), x(0, surface).y, C.y + r * s, 1);
      if (band.size() >= 3) FillShape(p, band, kElixir);
      glow_.Draw(p, C + glm::vec2(0.0f, 8.0f * s), 20.0f * s, WithAlpha(kElixir, 0.4f * level));
      const float dy = surface - centre.y;
      const float half = std::sqrt(std::max(0.0f, r * r - dy * dy));
      art::FilledEllipse(p, x(centre.x, surface), glm::vec2(half, 2.0f) * s, Hex(0xD8FFEE, 0.85f));
    }
    p.Ring(C, r * s, 1.8f * s, Hex(0xEDE4FF, 0.7f));
    p.Arc(C, 15.0f * s, 1.8f * s, -1.02f, 0.64f, Hex(0xFFFFFF, 0.5f));
  }
  DrawSanctumCandles(p, c, x, frame);
  // The motto and the map's end.
  if (frame.sigil && view_bottom > city::kSanctumMotto - 30.0f) {
    const std::string motto = "SOLVE ET COAGULA";
    const float size = 13.0f * kText * s, spacing = 4.0f * s;
    const glm::vec2 at = x(195, city::kSanctumMotto - 4.5f);
    Spaced(c, *frame.sigil, motto, at.x - SpacedWidth(*frame.sigil, motto, size, spacing) * 0.5f, at.y, size, spacing,
           Mixed(kHallDim, Hex(0xE6CC88), all));
    const std::string end = "the end of the map";
    const float small = 10.0f * kText * s, tracking = 2.0f * s;
    const glm::vec2 foot = x(195, city::kMapEnd - 3.5f);
    Spaced(c, *frame.sigil, end, foot.x - SpacedWidth(*frame.sigil, end, small, tracking) * 0.5f, foot.y, small,
           tracking, kHallDim);
  }
  // Asleep behind its gate: the floor lies dark under the mist, which lifts as it wakes.
  if (S.awake < 1.0f) p.FillRect({x(0, d.top), x(kWidth, city::kHeight)}, Hex(0x0A0812, 0.62f * (1.0f - S.awake)));
}

void CityArt::DrawSanctumCandles(Canvas& p, Canvas& c, const Xf& x, const CityFrame& frame) {
  const float s = x.s, t = frame.time, view_bottom = frame.scroll + frame.ViewHeight();
  const CityFrame::Sanctum& S = frame.sanctum;
  const bool shut = !frame.districts[2].open;
  const wlw::ui::Font& font = *frame.font;
  for (int i = 0; i < 15; ++i) {
    const size_t n = static_cast<size_t>(i);
    const Vec2 l = city::SanctumCandle(i);
    if (l.y + 70.0f < frame.scroll || l.y - 70.0f > view_bottom) continue;
    const glm::vec2 at = x(l);
    const auto& a = S.lamp[n];
    const bool locked = S.locked[n], perfect = a[0] >= 1.0f && a[1] >= 1.0f && a[2] >= 1.0f;
    const float lit = Clamp01(S.flame[n]);
    const bool next = i == S.breathe;
    if (lit > 0.0f) glow_.Draw(p, at, 56.0f * s, Hex(0xFF9E3D, 0.22f * lit));  // its pool of light
    const Rgba ring = perfect ? Hex(0xFFE6A6) : locked ? Hex(0x2A2236) : next ? kElixirLight : Hex(0x8C7CB0);
    p.Ring(at, 16.0f * s, (next ? 3.0f : 2.0f) * s, ring);
    p.Circle(at, 12.8f * s, locked ? Hex(0x3A3242) : Hex(0xB8A888));
    p.Circle(at, 11.2f * s, locked ? Hex(0x2A2430) : Hex(0xE8DCC0));
    if (lit > 0.0f) {
      const float k = static_cast<float>(i);
      const float flicker = frame.calm ? 0.0f : std::sin(t * 7.0f + k * 1.7f) * 1.2f + std::sin(t * 11.3f + k) * 0.8f;
      glow_.Draw(p, at, (22.0f + flicker) * s, Hex(0xFFB14D, 0.6f * lit));
      glow_.Draw(p, at, 7.0f * s, Hex(0xFFF2C8, lit));
    }
    if (S.flash[n] > 0.0f) p.Ring(at, 16.0f * s, 5.0f * s, Hex(0xFFFFFF, S.flash[n]));
    if (shut) continue;
    wlw::ui::TextStyle style{.color = Hex(0xF4F0FF), .outline = 0.1f, .outline_color = Hex(0x0A0810, 0.9f)};
    if (locked) {
      style = {.color = Hex(0xF4F0FF, 0.22f)};
    } else if (a[0] > 0.0f) {
      style = {
          .color = Hex(0x2A1A00), .outline = 0.1f, .outline_color = Rgba{1.0f, 240.0f / 255.0f, 210.0f / 255.0f, 0.7f}};
    }
    c.Text(font, std::to_string(i + 1), at, 13.0f * kText * s, Align::Center, style);
    if (locked) continue;
    for (int k = 0; k < 3; ++k) {
      const glm::vec2 star = x(city::CandleStar(i, k));
      const float on = Clamp01(a[static_cast<size_t>(k)]);
      const float r = on > 0.0f ? 4.5f + 2.5f * (1.0f - on) : 4.5f;
      StarShape(c, star, (r + 1.0f) * s, on > 0.0f ? Hex(0xB5781C, std::max(0.4f, on)) : Hex(0x4A3E5E, 0.9f));
      StarShape(c, star, r * s, on > 0.0f ? Hex(0xFFC24D, std::max(0.4f, on)) : Hex(0x1E1828, 0.9f));
    }
  }
  // The next to play breathes (quickly, a pulse, after a solve).
  if (S.breathe >= 0 && S.breathe < city::kDistrictLamps) {
    const glm::vec2 at = x(city::DistrictLamps(4)[static_cast<size_t>(S.breathe)]);
    float o = 0.8f;
    if (!frame.calm) {
      const float k = std::fmod(t, 0.6f) / 0.6f;
      o = S.pulse ? 1.0f - 0.85f * (1.0f - (1.0f - k) * (1.0f - k))
                  : 0.25f + 0.75f * (0.5f - 0.5f * std::cos(2.0f * kPi * t / 1.8f));
    }
    p.Ring(at, 19.0f * s, 3.0f * s, WithAlpha(kElixirLight, o));
  }
}

}  // namespace threadlock
