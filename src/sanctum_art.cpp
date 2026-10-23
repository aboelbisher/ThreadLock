#include "sanctum_art.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>

#include "polyline.h"
#include "workshop_art.h"
#include "wlw/rendering/shader.h"

namespace threadlock {
namespace {

using art::kPi;
using wlw::ui::Align;
using wlw::ui::Canvas;
using wlw::ui::Rect;
using wlw::ui::TextStyle;

// The table and the inlay.
constexpr Rgba kVein = Hex(0x8A78B8), kParchment = Hex(0xD8C49A), kQuill = Hex(0xCFC6E8);
constexpr Rgba kWax = Hex(0xE8DCC0), kWaxEdge = Hex(0xB8A888), kPool = Hex(0xFF9E3D), kFlame = Hex(0xFFB14D);
constexpr Rgba kFlameCore = Hex(0xFFF8E0);
constexpr Rgba kGold = Hex(0xC9A85A), kMark = Hex(0xE6CC88);
// The grooves: a gold lip (lit along one edge, shaded along the other) round a black core.
constexpr Rgba kLip = Hex(0xB49450), kLipLight = Hex(0xF8E7B0), kLipDark = Hex(0x3E3014), kCore = Hex(0x040307);
constexpr float kLipWidth = 18.0f, kCoreWidth = 13.0f, kBend = 6.0f;
// Glass.
constexpr Rgba kGlass = Hex(0xE8DEFF), kRim = Hex(0xEDE4FF), kGlobe = Hex(0x9C86E0), kShine = Hex(0xFFFFFF);
constexpr Rgba kSealGold = Hex(0xB8964E), kSealFace = Hex(0x1E1626), kSealEdge = Hex(0x5A4520);
constexpr Rgba kBarDash = Hex(0xF8E7B0), kBarGlow = Hex(0xFFF2C8), kSealFlash = Hex(0xFFE9A8);
constexpr Rgba kPillBack = Hex(0x0E0A1A, 0.9f), kPillInk = Hex(0xF4F0FF), kPillFull = Hex(0xC8FFE6);
constexpr Rgba kMixerTint = Hex(0x0B0812);
// The payoff: the ignite's gold and the elixir's column.
constexpr Rgba kIgnite = Hex(0xFFE9A8), kElixir = Hex(0xC8FFE6);
// The failures: sludge (the stain, its cloud, the clotted globe, bubbles), the lonely seal's red, the short's ghost.
constexpr Rgba kSludge = Hex(0x6E5642), kSludgeDash = Hex(0x7A6450), kCloud = Hex(0x8A6E52), kCloudDark = Hex(0x5E4A3A);
constexpr Rgba kClot = Hex(0x7A6048), kBubble = Hex(0xD2B490), kPhialMud = Hex(0x7A5E48), kPhialMudCore = Hex(0xA88A6C);
constexpr Rgba kFail = Hex(0xFF5A5A), kFailGlow = Hex(0xFF4D4D), kGhost = Hex(0xFF8A8A), kEmpty = Hex(0xFF6B6B);
constexpr Rgba kWait = Hex(0x9FC2FF);
// The moments' lengths (seconds).
constexpr float kSludgeLength = 1.4f, kPhialSludgeLength = 1.2f, kFailLength = 1.2f, kJamLength = 1.3f;
constexpr float kSettled = 100.0f;   // long past any moment
constexpr float kLitHold = 0.6f;     // the whole sigil lit, before the result card
constexpr float kSigilAfter = 0.7f;  // a finale's next sigil lights this much later

uint64_t Mix(uint64_t h, uint64_t v) {
  h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
  return h;
}

uint64_t Bits(float f) {
  uint32_t u = 0;
  std::memcpy(&u, &f, sizeof(u));
  return u;
}

glm::vec2 V(Vec2 p) { return {p.x, p.y}; }

float Ease(float k) {
  k = std::clamp(k, 0.0f, 1.0f);
  return k < 0.5f ? 2.0f * k * k : 1.0f - (2.0f - 2.0f * k) * (2.0f - 2.0f * k) * 0.5f;
}
float EaseOut(float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  return 1.0f - (1.0f - t) * (1.0f - t);
}
// A swell 0 -> 1 -> 0 over w.
float Bump(float u, float w) { return u <= 0.0f || u >= w ? 0.0f : std::sin(kPi * u / w); }

// A soft disc drawn over (not added to) what is under it: dark clouds, which an additive glow can't draw. Rings of
// it stacked from the edge in, `color`'s alpha reached at the middle.
void SoftDisc(Canvas& canvas, glm::vec2 c, float r, Rgba color) {
  constexpr int kSteps = 6;
  const float each = 1.0f - std::pow(1.0f - color.a, 1.0f / kSteps);
  for (int k = 0; k < kSteps; ++k)
    canvas.Circle(c, r * (1.0f - static_cast<float>(k) / kSteps), WithAlpha(color, each / std::max(color.a, 0.001f)));
}

float Length(const std::vector<glm::vec2>& line) {
  float total = 0.0f;
  for (size_t k = 0; k + 1 < line.size(); ++k) total += glm::distance(line[k], line[k + 1]);
  return total;
}

// A stroke along a polyline from distance `from` to `to` (canvas units; outside its ends it is clipped).
void Trace(Canvas& canvas, const std::vector<glm::vec2>& line, float from, float to, float width, Rgba color) {
  float walked = 0.0f;
  for (size_t k = 0; k + 1 < line.size(); ++k) {
    const float seg = glm::distance(line[k], line[k + 1]);
    if (seg > 0.0f && walked + seg > from && walked < to) {
      const float t0 = std::max(0.0f, from - walked) / seg, t1 = std::min(seg, to - walked) / seg;
      canvas.Line(line[k] + (line[k + 1] - line[k]) * t0, line[k] + (line[k + 1] - line[k]) * t1, width, color);
    }
    walked += seg;
  }
}

// Dashes along a polyline (canvas units), shifted `shift` along it.
void Dashes(Canvas& canvas, const std::vector<glm::vec2>& line, float on, float off, float shift, float width,
            Rgba color) {
  const float length = Length(line), period = on + off;
  if (period <= 0.0f) return;
  for (float d = -std::fmod(shift, period); d < length; d += period) Trace(canvas, line, d, d + on, width, color);
}

// A dashed ring: arcs `on` long with `off` between (canvas units).
void DashedRing(Canvas& canvas, glm::vec2 c, float r, float width, Rgba color, float on, float off, float turn = 0.0f) {
  if (r <= 0.0f) return;
  const int count = std::max(4, static_cast<int>(2.0f * kPi * r / (on + off)));
  const float step = 2.0f * kPi / static_cast<float>(count), sweep = step * on / (on + off);
  for (int k = 0; k < count; ++k) canvas.Arc(c, r, width, turn + step * static_cast<float>(k), sweep, color);
}

// A closed loop's points (a figure), the first again at the end.
std::vector<glm::vec2> Closed(std::vector<glm::vec2> points) {
  if (!points.empty()) points.push_back(points.front());
  return points;
}

// A circle as a polyline from angle `from` sweeping `sweep` (radians, clockwise), canvas units.
std::vector<glm::vec2> CircleLine(glm::vec2 c, float r, float from, float sweep, int steps = 64) {
  std::vector<glm::vec2> line;
  for (int k = 0; k <= steps; ++k) {
    const float a = from + sweep * static_cast<float>(k) / static_cast<float>(steps);
    line.push_back(c + glm::vec2(std::cos(a), std::sin(a)) * r);
  }
  return line;
}

// The part of a round vessel below `level` (canvas y): a chord and the arc under it.
std::vector<glm::vec2> Below(glm::vec2 c, float r, float level) {
  const float h = std::clamp(level - c.y, -r, r);
  if (h >= r) return {};
  const float w = std::sqrt(std::max(0.0f, r * r - h * h));
  const float right = std::atan2(h, w), left = kPi - right;
  std::vector<glm::vec2> points;
  for (int k = 0; k <= 16; ++k) {
    const float a = right + (left - right) * static_cast<float>(k) / 16.0f;
    points.push_back(c + glm::vec2(std::cos(a), std::sin(a)) * r);
  }
  return points;
}

// A cubic BÃ©zier's points (for the table's veins and the quill).
void Cubic(std::vector<glm::vec2>& out, glm::vec2 a, glm::vec2 b, glm::vec2 c, glm::vec2 d) {
  for (int k = out.empty() ? 0 : 1; k <= 16; ++k) {
    const float t = static_cast<float>(k) / 16.0f, u = 1.0f - t;
    out.push_back(u * u * u * a + 3.0f * u * u * t * b + 3.0f * u * t * t * c + t * t * t * d);
  }
}

// Seconds since a phial filled (its moment); settled (a long time) when there is none, or calm.
float PaidAge(const ArtFrame& frame, int cup) {
  if (frame.calm) return kSettled;
  for (const Marker& marker : frame.markers) {
    if (marker.kind == Marker::Paid && marker.node == cup) return marker.age;
  }
  return kSettled;
}

// A phial's order is filled: full, pure, nothing more ran over.
bool Served(const ArtFrame& frame, int cup) {
  if (!frame.board->Started()) return false;
  const CupState state = frame.board->Cup(cup);
  return state.count >= frame.level->nodes[static_cast<size_t>(cup)].count && !state.impure && !state.spilled;
}

float Progress(const ArtFrame& frame, const Marker& marker, float length) {
  return frame.calm ? 1.0f : std::clamp(marker.age / length, 0.0f, 1.0f);
}

const Marker* MomentOf(const ArtFrame& frame, Marker::Kind kind, int node) {
  for (const Marker& marker : frame.markers) {
    if (marker.kind == kind && marker.node == node) return &marker;
  }
  return nullptr;
}

bool HasPiece(const ArtFrame& frame, int node, PieceType type) {
  const int piece = frame.board->PieceAt(node);
  return piece >= 0 && frame.board->PieceInfo(piece).piece.type == type;
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

// An element's alchemical mark (16 wide) by the colour a flask pours: water, fire, earth, Venus, the Moon.
void ElementMark(Canvas& canvas, Color color, glm::vec2 c, float s, Rgba ink) {
  const float w = 1.5f * s, h = 7.0f * s, x = 8.0f * s;
  auto triangle = [&](bool down) {
    const float tip = down ? h : -h, base = down ? -h : h;
    canvas.Line(c + glm::vec2(-x, base), c + glm::vec2(x, base), w, ink);
    canvas.Line(c + glm::vec2(x, base), c + glm::vec2(0.0f, tip), w, ink);
    canvas.Line(c + glm::vec2(0.0f, tip), c + glm::vec2(-x, base), w, ink);
  };
  switch (color) {
    case Color::Blue: triangle(true); break;
    case Color::Amber: triangle(false); break;
    case Color::Green:
      triangle(true);
      canvas.Line(c + glm::vec2(-x, -1.0f * s), c + glm::vec2(x, -1.0f * s), w, ink);
      break;
    case Color::Rose:
      canvas.Ring(c + glm::vec2(0.0f, -3.0f * s), 5.0f * s, w, ink);
      canvas.Line(c + glm::vec2(0.0f, 2.0f * s), c + glm::vec2(0.0f, 9.0f * s), w, ink);
      canvas.Line(c + glm::vec2(-4.0f * s, 6.0f * s), c + glm::vec2(4.0f * s, 6.0f * s), w, ink);
      break;
    case Color::Pearl:
      canvas.Arc(c, 6.0f * s, w, kPi * 0.6f, kPi * 1.3f, ink);
      canvas.Arc(c + glm::vec2(2.5f * s, 0.0f), 4.6f * s, w, kPi * 0.7f, kPi * 0.6f, ink);
      break;
    default: break;
  }
}

}  // namespace

void SanctumArt::Create(wlw::app::Application& app, std::shared_ptr<wlw::ui::Font> sigil) {
  table_shader_ = wlw::rendering::Shader::FromFile("shaders/sanctum_table.shader");
  sigil_ = std::move(sigil);
  tiles_.Create(app.Window(), "Sanctum", Hex(0x100C17));
  AddTile();  // every board needs one: made up front, so the shader compiles in the warm-up
}

void SanctumArt::AddTile() {
  tiles_.Add();
  TileArt art;
  art.table = wlw::rendering::ShaderMaterial::Create(table_shader_);
  art.vignette = wlw::rendering::ShaderMaterial::Create(table_shader_);
  art.table->SetParam("uLayer", 0.0f);
  art.vignette->SetParam("uLayer", 1.0f);
  tile_art_.push_back(std::move(art));
}

void SanctumArt::Invalidate() { tiles_.Invalidate(); }

void SanctumArt::Release() {
  tiles_.Shrink(1);
  tile_art_.resize(1);
  Invalidate();
}

void SanctumArt::Prepare(const ArtFrame& frame) {
  const Level& level = *frame.level;
  if (level_ == &level && grooves_.size() == level.pipes.size()) return;
  level_ = &level;
  sigils_ = level.look.sigils;
  grooves_.clear();
  for (const Pipe& pipe : level.pipes) {
    std::vector<glm::vec2> points;
    for (const Vec2 p : sigil::GroovePoints(level, pipe)) points.push_back(V(p));
    grooves_.push_back(art::RoundCorners(points, kBend, 1.5f));
  }
  vessels_.assign(level.nodes.size(), {});
  globes_.assign(level.nodes.size(), 0.0f);
  ignites_.assign(level.nodes.size(), {});
  released_.assign(level.nodes.size(), -1000000);
  for (size_t n = 0; n < level.nodes.size(); ++n) {
    const int node = static_cast<int>(n);
    switch (level.nodes[n].kind) {
      case NodeKind::Spout: vessels_[n] = sigil::FlaskAt(level, node); break;
      case NodeKind::Cup:
        vessels_[n] = sigil::PhialAt(level, node);
        ignites_[n] = sigil::IgniteFrom(level, node);
        break;
      default:
        if (IsSocketKind(level.nodes[n].kind)) globes_[n] = sigil::GlobeRadius(level, node);
        break;
    }
  }
  runes_.clear();
  figures_.clear();
  for (const Sigil& sigil : sigils_) {
    const float outer = sigil.rings.empty() ? 0.0f : *std::max_element(sigil.rings.begin(), sigil.rings.end());
    runes_.push_back(sigil::Runes(level, sigil, outer));
    figures_.push_back(sigil::FigureLines(level, sigil));
  }
  candles_ = sigil::Candles(level);
  stains_.assign(level.pipes.size(), {});
}

std::vector<glm::vec2> SanctumArt::OnCanvas(const ArtFrame& frame, const std::vector<Vec2>& points) const {
  std::vector<glm::vec2> line;
  for (const Vec2 p : points) line.push_back(frame.layout.ToCanvas(p));
  return line;
}

std::vector<glm::vec2> SanctumArt::OnCanvas(const ArtFrame& frame, const std::vector<glm::vec2>& points) const {
  std::vector<glm::vec2> line;
  for (const glm::vec2 p : points) line.push_back(frame.layout.ToCanvas({p.x, p.y}));
  return line;
}

// ---- The still picture ----

void SanctumArt::DrawBack(Canvas& picture, const ArtFrame& frame) {
  Prepare(frame);
  const glm::vec2 screen = picture.Size();
  const float h = screen.y;
  const float scroll = frame.layout.scroll * frame.layout.scale;  // canvas units
  const float total = h + frame.layout.scroll_max * frame.layout.scale;
  const TilePool::Span span = TilePool::Visible(scroll, h, total);
  const Layout layout = frame.layout.Unscrolled();
  uint64_t key = Mix(0, reinterpret_cast<uintptr_t>(frame.level));
  key = Mix(key, Bits(layout.origin.x));
  key = Mix(key, Bits(layout.origin.y));
  key = Mix(key, Bits(layout.scale));
  key = Mix(key, Bits(layout.scroll_max));
  key = Mix(key, Bits(screen.x));
  key = Mix(key, Bits(screen.y));
  key = Mix(key, Bits(picture.PixelsPerUnit()));
  while (tiles_.Count() < std::min(span.count, 3)) AddTile();
  for (int k = span.first; k <= span.last; ++k) {
    const int slot = tiles_.Hold(k, span.first, span.last);
    if (slot < 0 || tiles_[slot].fresh) continue;
    const uint64_t base = Mix(key, static_cast<uint64_t>(k) + 1u);
    if (tiles_[slot].base == base) continue;
    DrawStill(slot, k, frame, screen, total);
    tiles_[slot].base = tiles_[slot].key = base;
  }
  tiles_.Draw(picture, span.first, span.last, scroll);
  tiles_.EndFrame();

  DrawCandles(picture, frame);
  DrawStains(picture, frame);
  for (size_t n = 0; n < frame.level->nodes.size(); ++n) DrawMixerBack(picture, frame, static_cast<int>(n));
}

void SanctumArt::DrawStill(int slot, int k, const ArtFrame& whole, glm::vec2 screen, float total) {
  TilePool::Slot& tile = tiles_[slot];
  TileArt& art = tile_art_[static_cast<size_t>(slot)];
  Canvas& canvas = tile.view->GetCanvas();
  ArtFrame frame = whole;
  frame.layout = whole.layout.Unscrolled();
  frame.layout.origin.y -= static_cast<float>(k) * screen.y;
  tile.view->SetUpdateMode(wlw::scene::Viewport::UpdateMode::Once);

  for (const auto& material : {art.table, art.vignette}) {
    material->SetParam("uCanvas", screen);
    material->SetParam("uOffset", glm::vec2(0.0f, static_cast<float>(k) * screen.y));
    material->SetParam("uTotal", glm::vec2(screen.x, total));
    material->SetParam("uScreen", screen);
    material->SetParam("uScale", frame.Scale());
  }
  canvas.ShaderRect({{0.0f, 0.0f}, screen}, art.table);
  DrawTable(canvas, frame);
  for (size_t i = 0; i < sigils_.size(); ++i) DrawInlay(canvas, frame, sigils_[i], i);
  DrawGrooves(canvas, frame);
  DrawGlass(canvas, frame);
  canvas.ShaderRect({{0.0f, 0.0f}, screen}, art.vignette);
}

void SanctumArt::DrawTable(Canvas& canvas, const ArtFrame& frame) const {
  const Level& level = *frame.level;
  const float s = frame.Scale();
  auto at = [&](float x, float y, float dy) { return frame.layout.ToCanvas({x, y + dy}); };
  const int screens = std::max(1, static_cast<int>(std::ceil(level.look.height / kBoardSize.y - 0.01f)));
  for (int screen = 0; screen < screens; ++screen) {
    // The obsidian's veins, faint (the mock's four curves, a set per screen).
    const float dy = kBoardSize.y * static_cast<float>(screen);
    std::vector<std::vector<glm::vec2>> veins(4);
    Cubic(veins[0], at(-35, 174, dy), at(65, 144, dy), at(105, 244, dy), at(205, 214, dy));
    Cubic(veins[0], at(205, 214, dy), at(305, 184, dy), at(345, 134, dy), at(405, 174, dy));
    Cubic(veins[1], at(-35, 474, dy), at(45, 514, dy), at(165, 434, dy), at(245, 494, dy));
    Cubic(veins[1], at(245, 494, dy), at(325, 554, dy), at(345, 554, dy), at(405, 514, dy));
    Cubic(veins[2], at(105, -106, dy), at(125, 34, dy), at(75, 114, dy), at(115, 234, dy));
    Cubic(veins[3], at(285, 774, dy), at(265, 674, dy), at(315, 614, dy), at(285, 554, dy));
    for (const std::vector<glm::vec2>& vein : veins) art::Polyline(canvas, vein, 1.2f * s, WithAlpha(kVein, 0.08f));
  }

  // The parchment and the quill at the board's lower left (mirrored to the right if a vessel is there; none if
  // both corners are taken).
  const float dy = level.look.height - kBoardSize.y;
  auto blocked = [&](bool mirror) {
    for (size_t n = 0; n < level.nodes.size(); ++n) {
      const sigil::Vessel& v = vessels_[n];
      if (v.r <= 0.0f) continue;
      const float x = mirror ? 360.0f - v.centre.x : v.centre.x;
      if (x - v.r < 135.0f && v.centre.y + v.r > 470.0f + dy) return true;
    }
    return false;
  };
  const bool mirror = blocked(false);
  if (!mirror || !blocked(true)) {
    const float turn = -8.0f / 180.0f * kPi, cs = std::cos(turn), sn = std::sin(turn);
    auto place = [&](float x, float y) {  // the mock's rotation about (65, 554), then mirrored
      const float rx = 65.0f + (x - 65.0f) * cs - (y - 554.0f) * sn, ry = 554.0f + (x - 65.0f) * sn + (y - 554.0f) * cs;
      return frame.layout.ToCanvas({mirror ? 360.0f - rx : rx, ry + dy});
    };
    art::FillConvex(canvas, {place(-5, 504), place(123, 504), place(123, 608), place(-5, 608)},
                    WithAlpha(kParchment, 0.09f));
    const float rows[][2] = {{524, 109}, {538, 101}, {552, 113}, {566, 83}};
    for (const auto& row : rows)
      canvas.Line(place(7, row[0]), place(row[1], row[0]), 1.2f * s, WithAlpha(kParchment, 0.12f));
    std::vector<glm::vec2> quill;
    auto q = [&](float x, float y) { return frame.layout.ToCanvas({mirror ? 360.0f - x : x, y + dy}); };
    Cubic(quill, q(127, 480), q(107, 516), q(97, 550), q(93, 600));
    Cubic(quill, q(93, 600), q(107, 556), q(119, 522), q(135, 488));
    art::FillConvex(canvas, quill, WithAlpha(kQuill, 0.12f));
  }

  // The candles' wax (their flames and light are drawn each frame).
  for (const Vec2 c : candles_) {
    const glm::vec2 p = frame.layout.ToCanvas(c);
    canvas.Circle(p, 12.0f * s, WithAlpha(kWax, 0.9f));
    canvas.Ring(p, 12.0f * s, 2.0f * s, kWaxEdge);
    canvas.Circle(p + glm::vec2(8.0f, 9.0f) * s, 3.0f * s, kWax);
    canvas.Circle(p + glm::vec2(-10.0f, 6.0f) * s, 2.2f * s, kWax);
  }
}

void SanctumArt::DrawInlay(Canvas& canvas, const ArtFrame& frame, const Sigil& sigil, size_t index) const {
  if (sigil.rings.empty()) return;
  const float s = frame.Scale();
  const glm::vec2 c = frame.layout.ToCanvas(sigil.at);
  const float outer = *std::max_element(sigil.rings.begin(), sigil.rings.end());
  const float r = outer * s;
  glow_.Draw(canvas, c, r + 22.0f * s, WithAlpha(kGold, 0.05f));
  canvas.Ring(c, r + 2.0f * s, 2.4f * s, kGold);
  canvas.Ring(c, r - 10.0f * s, 1.0f * s, kGold);
  for (const sigil::Stroke2& rune : runes_[index])
    canvas.Line(frame.layout.ToCanvas(rune.a), frame.layout.ToCanvas(rune.b), 1.2f * s, WithAlpha(kGold, 0.75f));
  DashedRing(canvas, c, 0.69f * r, 0.8f * s, WithAlpha(kGold, 0.45f), 4.0f * s, 6.0f * s);
  canvas.Ring(c, 0.41f * r, 1.3f * s, WithAlpha(kGold, 0.8f));
  for (const float ring : sigil.rings) {
    if (ring < outer) canvas.Ring(c, ring * s, 1.2f * s, WithAlpha(kGold, 0.6f));
  }
  const std::vector<std::vector<Vec2>>& figures = figures_[index];
  for (size_t f = 0; f < figures.size(); ++f) {
    art::Polyline(canvas, Closed(OnCanvas(frame, figures[f])), (f == 0 ? 1.4f : 1.1f) * s,
                  WithAlpha(kGold, f == 0 ? 0.7f : 0.45f));
  }
  canvas.Circle(c, 3.5f * s, kGold);
}

void SanctumArt::DrawGrooves(Canvas& canvas, const ArtFrame& frame) const {
  const float s = frame.Scale();
  std::vector<art::Path> paths;
  for (const std::vector<glm::vec2>& groove : grooves_) paths.push_back(art::MakePath(OnCanvas(frame, groove)));
  // Every lip first, then every core, so where grooves meet the black runs through.
  for (const art::Path& path : paths) {
    art::Stroke(canvas, path, 0.0f, kLipWidth * s, kLip);
    art::Stroke(canvas, path, -7.6f * s, 1.2f * s, WithAlpha(kLipLight, 0.6f));
    art::Stroke(canvas, path, 7.6f * s, 1.2f * s, WithAlpha(kLipDark, 0.8f));
  }
  for (const art::Path& path : paths) art::Stroke(canvas, path, 0.0f, kCoreWidth * s, kCore);
}

void SanctumArt::DrawGlass(Canvas& canvas, const ArtFrame& frame) const {
  const Level& level = *frame.level;
  const float s = frame.Scale();
  for (size_t n = 0; n < level.nodes.size(); ++n) {
    const Node& node = level.nodes[n];
    const glm::vec2 at = frame.At(static_cast<int>(n));
    const sigil::Vessel& v = vessels_[n];
    if (v.r > 0.0f) {
      // A flask or a phial: its neck and gold collar, its body, the rim and a shine.
      const bool flask = node.kind == NodeKind::Spout;
      const glm::vec2 c = frame.layout.ToCanvas(v.centre), up = V(v.up), across{-up.y, up.x};
      const float r = v.r * s, neck = (flask ? sigil::kFlaskNeck : sigil::kPhialNeck) * s;
      const float half = (flask || v.r > 20.0f ? 6.0f : 4.5f) * s;
      const glm::vec2 a = c + up * (r - 2.0f * s), b = c + up * (r + neck);
      art::FillConvex(canvas, {a - across * half, a + across * half, b + across * half, b - across * half},
                      WithAlpha(kGlass, 0.08f));
      canvas.Line(a - across * half, b - across * half, 1.5f * s, WithAlpha(kRim, 0.6f));
      canvas.Line(a + across * half, b + across * half, 1.5f * s, WithAlpha(kRim, 0.6f));
      canvas.Line(b - across * (half + 2.0f * s), b + across * (half + 2.0f * s), 5.0f * s, kLip);
      canvas.Circle(c, r, WithAlpha(kGlass, 0.06f));
      canvas.Ring(c, r, 1.8f * s, WithAlpha(kRim, 0.65f));
      canvas.Arc(c, 0.85f * r, 2.0f * s, 200.0f / 180.0f * kPi, 50.0f / 180.0f * kPi, WithAlpha(kShine, 0.5f));
      if (!flask && v.up.y < -0.7f)
        art::FilledEllipse(canvas, c + glm::vec2(0.0f, r + 1.0f * s), {0.8f * r, 3.0f * s}, kLip);
      if (flask && !node.beads.empty()) {
        // The element's mark beside the neck (below a flask fed sideways).
        const Vec2 out = node.outs.empty() ? Vec2{0, 1} : [&] {
          const std::vector<glm::vec2>& g = grooves_[static_cast<size_t>(node.outs[0])];
          const glm::vec2 d = g.size() > 1 ? glm::normalize(g[1] - g[0]) : glm::vec2(0, 1);
          return Vec2{d.x, d.y};
        }();
        const float side = v.centre.x < 180.0f ? 1.0f : -1.0f;
        const Vec2 mark = std::fabs(out.x) > 0.7f ? Vec2{v.centre.x, v.centre.y + v.r + 20.0f}
                                                  : Vec2{v.centre.x + side * (v.r + 19.0f), v.centre.y - 15.0f};
        ElementMark(canvas, node.beads[0].a, frame.layout.ToCanvas(mark), s, WithAlpha(kMark, 0.85f));
      }
      continue;
    }
    if (globes_[n] <= 0.0f) continue;
    if (TwinOf(level, static_cast<int>(n)) >= 0) {
      // A twin half: its seal's gold base (the leaves are drawn while a twin gate holds it).
      canvas.Circle(at + glm::vec2(0.0f, 3.0f) * s, 17.0f * s, Hex(0x000000, 0.35f));
      canvas.Circle(at, 15.0f * s, kSealGold);
      canvas.Circle(at, 11.0f * s, kSealFace);
      canvas.Ring(at, 15.0f * s, 1.0f * s, WithAlpha(kLipLight, 0.5f));
      continue;
    }
    // A glass globe; a socket's is bigger (the piece shows in it) with a dashed gold ring.
    const bool socket = node.socket;
    const float r = (socket ? globes_[n] - 2.0f : globes_[n]) * s;
    canvas.Circle(at, r, WithAlpha(kGlobe, 0.06f));
    canvas.Arc(at, 0.7f * r, 1.6f * s, 200.0f / 180.0f * kPi, 50.0f / 180.0f * kPi, WithAlpha(kShine, 0.45f));
    canvas.Ring(at, r, 1.5f * s, WithAlpha(kRim, 0.55f));
    if (socket) {
      DashedRing(canvas, at, r + 4.0f * s, 1.2f * s, WithAlpha(kGold, 0.7f), 2.6f * s, 3.0f * s);
    } else {
      canvas.Ring(at, r + 4.0f * s, 1.2f * s, WithAlpha(kGold, 0.6f));
    }
  }
}

// ---- Each frame, on the picture ----

void SanctumArt::DrawCandles(Canvas& picture, const ArtFrame& frame) const {
  const float s = frame.Scale(), t = frame.time;
  for (size_t i = 0; i < candles_.size(); ++i) {
    const float k = static_cast<float>(i);
    const float flicker = frame.calm ? 0.0f : std::sin(7.0f * t + 1.7f * k) * 1.2f + std::sin(11.3f * t + k) * 0.8f;
    const glm::vec2 p = frame.layout.ToCanvas(candles_[i]);
    glow_.Draw(picture, p, 62.0f * s, WithAlpha(kPool, 0.22f * (0.9f + 0.04f * flicker)));
    glow_.Draw(picture, p, (16.0f + flicker) * s, WithAlpha(kFlame, 0.7f));
    picture.Circle(p, 3.0f * s, kFlameCore);
  }
}

void SanctumArt::DrawStains(Canvas& picture, const ArtFrame& frame) {
  const Board& board = *frame.board;
  if (!board.Started()) {
    std::fill(stains_.begin(), stains_.end(), Stain{});
    return;
  }
  for (const Drop& drop : board.Drops()) {
    if (!drop.alive || drop.loc.kind != Loc::OnPipe) continue;
    Stain& stain = stains_[static_cast<size_t>(drop.loc.index)];
    if (stain.color != drop.bead.a) stain = {drop.bead.a, frame.time};
  }
  const float s = frame.Scale();
  for (size_t p = 0; p < stains_.size(); ++p) {
    const Stain& stain = stains_[p];
    if (stain.color == Color::None) continue;
    const float in = frame.calm ? 1.0f : std::min(1.0f, (frame.time - stain.since) / 0.5f);
    art::Polyline(picture, OnCanvas(frame, grooves_[p]), 7.0f * s,
                  WithAlpha(LiquidColor(*frame.palette, stain.color), 0.13f * in));
  }
}

void SanctumArt::DrawMixerBack(Canvas& picture, const ArtFrame& frame, int node) const {
  if (!HasPiece(frame, node, PieceType::Mixer)) return;
  const NodeAnim& anim = frame.anims[static_cast<size_t>(node)];
  const float s = frame.Scale();
  const glm::vec2 at = frame.At(node);
  const float recent =
      frame.calm ? 0.0f : std::clamp(1.0f - (frame.Clock() - static_cast<float>(anim.mix)) * 0.125f / 1.4f, 0.0f, 1.0f);
  const bool mud = anim.mixed == Color::Mud;
  const Rgba color = anim.mixed == Color::None ? LiquidColor(*frame.palette, Color::Green)
                     : mud                     ? kCloud
                                               : LiquidColor(*frame.palette, anim.mixed);
  glow_.Draw(picture, at, 40.0f * s, WithAlpha(color, (0.12f + 0.3f * recent) * (mud ? 0.6f : 1.0f)));
  picture.Circle(at, 24.0f * s, WithAlpha(kMixerTint, 0.35f));
}

// ---- The overlay ----

void SanctumArt::DrawFront(Canvas& canvas, const ArtFrame& frame) {
  Prepare(frame);
  const Level& level = *frame.level;
  DrawSolved(canvas, frame);
  for (size_t n = 0; n < level.nodes.size(); ++n) {
    const int node = static_cast<int>(n);
    switch (level.nodes[n].kind) {
      case NodeKind::Spout: DrawFlask(canvas, frame, node); break;
      case NodeKind::Cup:
        DrawPayoff(canvas, frame, node);
        DrawPhial(canvas, frame, node);
        DrawPill(canvas, frame, node);
        break;
      case NodeKind::Rack:
      case NodeKind::Mill:
      case NodeKind::Gate: DrawMachinePart(canvas, frame, node); break;  // not in World 4: the shared parts
      default:
        if (HasPiece(frame, node, PieceType::Mixer)) DrawMixer(canvas, frame, node);
        if (level.nodes[n].twin >= 0) DrawSeals(canvas, frame, node, level.nodes[n].twin);
        break;
    }
  }
}

void SanctumArt::DrawFlask(Canvas& canvas, const ArtFrame& frame, int spout) const {
  const Node& node = frame.level->nodes[static_cast<size_t>(spout)];
  const sigil::Vessel& v = vessels_[static_cast<size_t>(spout)];
  const float s = frame.Scale();
  const glm::vec2 c = frame.layout.ToCanvas(v.centre);
  const float r = v.r * s;
  const int total = static_cast<int>(node.beads.size());
  const int left = total - (frame.board->Started() ? frame.board->Poured(spout) : 0);
  // A lonely seal's partner flask, empty: its fill is gone (the jam's moment marks it).
  if (left > 0 && total > 0) {
    const Rgba color = LiquidColor(*frame.palette, node.beads[static_cast<size_t>(total - left)].a);
    const float level = c.y + r - 2.0f * r * (0.22f + 0.63f * static_cast<float>(left) / static_cast<float>(total));
    glow_.Draw(canvas, c + glm::vec2(0.0f, r * 0.3f), r + 10.0f * s, WithAlpha(color, 0.16f));
    const std::vector<glm::vec2> fill = Below(c, r - 1.5f * s, level);
    if (fill.size() > 2) {
      art::FillConvex(canvas, fill, color);
      canvas.Line(fill.front(), fill.back(), 2.4f * s, WithAlpha(Mixed(color, Hex(0xFFFFFF), 0.7f), 0.8f));
    }
  }
  canvas.Ring(c, r, 1.8f * s, WithAlpha(kRim, 0.4f));
  canvas.Arc(c, 0.85f * r, 2.0f * s, 200.0f / 180.0f * kPi, 50.0f / 180.0f * kPi, WithAlpha(kShine, 0.4f));
}

void SanctumArt::DrawPhial(Canvas& canvas, const ArtFrame& frame, int cup) const {
  const Node& node = frame.level->nodes[static_cast<size_t>(cup)];
  const sigil::Vessel& v = vessels_[static_cast<size_t>(cup)];
  const float s = frame.Scale();
  const glm::vec2 c = frame.layout.ToCanvas(v.centre);
  const float r = v.r * s;
  const CupState state = frame.board->Started() ? frame.board->Cup(cup) : CupState{};
  const float fill =
      std::clamp(frame.cup_fill[static_cast<size_t>(cup)] / static_cast<float>(std::max(1, node.count)), 0.0f, 1.0f) *
      0.82f;
  if (fill > 0.005f) {
    const bool mud = state.impure;
    const Rgba color = mud ? kPhialMud : LiquidColor(*frame.palette, node.want);
    const Rgba core = mud ? kPhialMudCore : Mixed(color, Hex(0xFFFFFF), 0.7f);
    if (!mud) glow_.Draw(canvas, c, r + 18.0f * s, WithAlpha(color, 0.12f + 0.05f * fill));
    const float level = c.y + r - 2.0f * r * fill;
    const std::vector<glm::vec2> body = Below(c, r - 1.5f * s, level);
    if (body.size() > 2) {
      art::FillConvex(canvas, body, color);
      const float w = 0.5f * glm::distance(body.front(), body.back());
      art::FilledEllipse(canvas, {c.x, level}, {w, 2.4f * s}, WithAlpha(core, 0.85f));
    }
  }
  canvas.Ring(c, r, 2.0f * s, WithAlpha(kRim, 0.45f));
  canvas.Arc(c, 0.85f * r, 2.0f * s, 200.0f / 180.0f * kPi, 50.0f / 180.0f * kPi, WithAlpha(kShine, 0.4f));
  // Sludge in it: a few bubbles rising.
  if (const Marker* mud = MomentOf(frame, Marker::Mud, cup)) {
    for (int j = 0; j < 3; ++j) {
      const float ph = std::fmod(mud->age * 0.8f + static_cast<float>(j) * 0.37f, 1.0f);
      const glm::vec2 b = c + glm::vec2((static_cast<float>(j) - 1.0f) * 0.4f * r, r * (0.6f - 0.9f * ph));
      canvas.Ring(b, (1.5f + 2.0f * ph) * s, 1.2f * s, WithAlpha(kBubble, 0.8f * (1.0f - ph)));
    }
  }
}

bool SanctumArt::Bad(const ArtFrame& frame, int cup) const {
  if (!frame.board->Started()) return false;
  const CupState& state = frame.board->Cup(cup);
  if (state.impure || state.spilled) return true;
  if (MomentOf(frame, Marker::Jam, -1) && state.count < frame.level->nodes[static_cast<size_t>(cup)].count) return true;
  for (const Marker::Kind kind : {Marker::WrongCup, Marker::Dry, Marker::Spill, Marker::Mud}) {
    if (MomentOf(frame, kind, cup)) return true;
  }
  return false;
}

void SanctumArt::DrawPill(Canvas& canvas, const ArtFrame& frame, int cup) const {
  const Node& node = frame.level->nodes[static_cast<size_t>(cup)];
  const CupState state = frame.board->Started() ? frame.board->Cup(cup) : CupState{};
  const float s = frame.Scale();
  char count[24];
  std::snprintf(count, sizeof(count), "%d/%d", state.count, node.count);
  const bool bad = Bad(frame, cup), full = state.count >= node.count;
  const Rgba want = LiquidColor(*frame.palette, node.want);
  const float text = frame.font->Measure(count, 13.0f * s);
  const float width = text + 30.0f * s, height = sigil::kPillHeight * s;
  const glm::vec2 c = frame.layout.ToCanvas(sigil::PillAt(*frame.level, cup));
  const glm::vec2 half{width * 0.5f, height * 0.5f};
  canvas.FillRect({c - half - glm::vec2(1.0f * s), c + half + glm::vec2(1.0f * s)},
                  bad ? WithAlpha(kFail, 0.75f) : WithAlpha(want, 0.55f), height * 0.5f + 1.0f * s);
  canvas.FillRect({c - half, c + half}, kPillBack, height * 0.5f);
  art::Glyph(canvas, node.want, {c.x - half.x + 12.0f * s, c.y}, 4.5f * s, want);
  canvas.Text(*frame.font, count, {c.x + 7.0f * s, c.y}, 13.0f * s, Align::Center,
              TextStyle{.color = bad    ? kGhost
                                 : full ? kPillFull
                                        : kPillInk});
}

void SanctumArt::DrawMixer(Canvas& canvas, const ArtFrame& frame, int node) const {
  const NodeAnim& anim = frame.anims[static_cast<size_t>(node)];
  const float s = frame.Scale();
  const glm::vec2 at = frame.At(node);
  const float recent =
      frame.calm ? 0.0f : std::clamp(1.0f - (frame.Clock() - static_cast<float>(anim.mix)) * 0.125f / 1.4f, 0.0f, 1.0f);
  const Rgba color = anim.mixed == Color::None  ? LiquidColor(*frame.palette, Color::Green)
                     : anim.mixed == Color::Mud ? kCloud
                                                : LiquidColor(*frame.palette, anim.mixed);
  // The swirl round the medallion, turning with the board's clock.
  const float turn = frame.calm ? 0.0f : frame.Clock() * 22.0f / 180.0f * kPi;
  const float alpha = 0.25f + 0.75f * recent;
  canvas.Arc(at, 26.5f * s, 3.4f * s, turn, 2.1f, WithAlpha(color, alpha));
  canvas.Arc(at, 24.5f * s, 3.0f * s, turn + kPi, 2.1f, WithAlpha(color, alpha * 0.8f));
}

void SanctumArt::DrawSeals(Canvas& canvas, const ArtFrame& frame, int half, int twin) {
  if (!HasPiece(frame, half, PieceType::TwinGate)) return;
  const Board& board = *frame.board;
  const float s = frame.Scale();
  int& released = released_[static_cast<size_t>(half)];
  const int a_tick = frame.anims[static_cast<size_t>(half)].tick, b_tick = frame.anims[static_cast<size_t>(twin)].tick;
  if (!board.Started())
    released = -1000000;
  else if (a_tick == b_tick && a_tick > -1000000)
    released = a_tick;
  const float open = frame.calm ? 0.0f : Bump(frame.Clock() - static_cast<float>(released) + 0.5f, 3.0f);

  const glm::vec2 a = frame.At(half), b = frame.At(twin);
  const glm::vec2 dir = glm::normalize(b - a);
  const glm::vec2 from = a + dir * 15.0f * s, to = b - dir * 15.0f * s;
  canvas.Line(from + glm::vec2(1.0f, 3.0f) * s, to + glm::vec2(1.0f, 3.0f) * s, 5.0f * s, Hex(0x000000, 0.5f));
  canvas.Line(from, to, 3.0f * s, kLip);
  Dashes(canvas, {from, to}, 4.0f * s, 3.0f * s, -frame.Clock() * 3.0f * s, 1.2f * s, kBarDash);
  if (open > 0.01f) {
    canvas.Line(from, to, 3.0f * s, WithAlpha(kBarGlow, 0.9f * open));
    for (int k = 0; k <= 4; ++k)
      glow_.Draw(canvas, from + (to - from) * (static_cast<float>(k) / 4.0f), 9.0f * s,
                 WithAlpha(kBarGlow, 0.4f * open));
  }
  // The seals: a gold disc, its dark face, two leaves sliding apart along the bar as the pair lets go.
  for (const int n : {half, twin}) {
    const glm::vec2 c = frame.At(n);
    if (open > 0.01f) glow_.Draw(canvas, c, (16.0f + 8.0f * open) * s, WithAlpha(kSealFlash, 0.8f * open));
    canvas.Circle(c, 15.0f * s, kSealGold);
    canvas.Circle(c, 11.0f * s, kSealFace);
    const float angle = std::atan2(dir.y, dir.x);
    for (const float side : {-1.0f, 1.0f}) {
      const glm::vec2 centre = c + dir * (side * 5.5f * open * s);
      std::vector<glm::vec2> leaf{centre};
      const float start = angle + (side < 0.0f ? kPi * 0.5f : -kPi * 0.5f);
      for (int k = 0; k <= 8; ++k)
        leaf.push_back(centre +
                       glm::vec2(std::cos(start + kPi * k / 8.0f), std::sin(start + kPi * k / 8.0f)) * 9.0f * s);
      art::FillConvex(canvas, leaf, kSealGold);
      leaf.push_back(centre);
      art::Polyline(canvas, leaf, 0.8f * s, kSealEdge);
    }
    canvas.Ring(c, 11.0f * s, 1.0f * s, WithAlpha(kLipLight, 0.7f));
  }

  // A drop waiting at one seal while the other has none: it waits for its twin (unless the jam says more).
  if (!board.Running() || MomentOf(frame, Marker::Jam, -1)) return;
  auto holds = [&](int n) {
    for (const Drop& drop : board.Drops()) {
      if (drop.alive && drop.loc.kind == Loc::InNode && drop.loc.index == n) return true;
    }
    return false;
  };
  for (const auto& [n, other] : {std::pair{half, twin}, std::pair{twin, half}}) {
    if (!holds(n) || holds(other)) continue;
    const glm::vec2 c = frame.At(n);
    DashedRing(canvas, c, 21.0f * s, 1.3f * s, WithAlpha(kWait, 0.75f), 2.4f * s, 2.6f * s);
    const bool right = frame.level->nodes[static_cast<size_t>(n)].at.x > 250.0f;
    canvas.Text(*sigil_, "waits for its twin", c + glm::vec2(right ? -26.0f : 26.0f, 32.0f) * s, 10.5f * s,
                right ? Align::Right : Align::Left, TextStyle{.color = WithAlpha(kWait, 0.95f)});
  }
}

void SanctumArt::DrawPayoff(Canvas& canvas, const ArtFrame& frame, int cup) const {
  if (!Served(frame, cup)) return;
  const float a = PaidAge(frame, cup), s = frame.Scale();
  const sigil::Vessel& v = vessels_[static_cast<size_t>(cup)];
  const glm::vec2 c = frame.layout.ToCanvas(v.centre);
  const Rgba color = LiquidColor(*frame.palette, frame.level->nodes[static_cast<size_t>(cup)].want);
  // The elixir: a glow swelling round the phial and a column of light rising from its neck.
  const float rise = std::min(1.0f, a / 0.6f);
  glow_.Draw(canvas, c, (28.0f + 24.0f * EaseOut(rise)) * s * v.r / 24.0f,
             WithAlpha(color, 0.5f * rise * std::max(0.4f, std::clamp(1.0f - (a - 2.0f) / 2.0f, 0.0f, 1.0f))));
  const float column = std::clamp(1.4f - a / 2.5f, 0.0f, 1.0f);
  if (column > 0.0f) {
    const glm::vec2 up = V(v.up);
    const glm::vec2 top = c + up * (v.r + sigil::kPhialNeck) * s;
    const glm::vec2 end = top + up * 84.0f * EaseOut(std::min(1.0f, a / 0.55f)) * s;
    glow_.Draw(canvas, (top + end) * 0.5f, 0.5f * glm::distance(top, end) + 10.0f * s,
               WithAlpha(kElixir, 0.3f * column));
    canvas.Line(top, end, 5.0f * s, WithAlpha(kElixir, column));
  }
  // Its grooves ignite back toward the flasks, and stay lit.
  for (const sigil::Ignite& ignite : ignites_[static_cast<size_t>(cup)]) {
    const float u = Ease((a - ignite.delay) / std::max(ignite.length, 0.01f));
    if (u <= 0.0f) continue;
    std::vector<glm::vec2> line = OnCanvas(frame, grooves_[static_cast<size_t>(ignite.pipe)]);
    std::reverse(line.begin(), line.end());
    const float alpha = a > ignite.delay + ignite.length + 1.2f ? 0.6f : 0.95f;
    const float length = Length(line) * u;
    Trace(canvas, line, 0.0f, length, 7.0f * s, WithAlpha(kIgnite, 0.22f * alpha));
    Trace(canvas, line, 0.0f, length, 3.0f * s, WithAlpha(kIgnite, alpha));
  }
  // A twin pair both of whose halves are lit: its bar lights from both ends to the middle.
  const Level& level = *frame.level;
  for (size_t n = 0; n < level.nodes.size(); ++n) {
    const int twin = level.nodes[n].twin;
    if (twin < 0) continue;
    float reached = -1.0f;
    bool both = true;
    for (const int half : {static_cast<int>(n), twin}) {
      float at = -1.0f;
      for (const sigil::Ignite& ignite : ignites_[static_cast<size_t>(cup)]) {
        if (level.pipes[static_cast<size_t>(ignite.pipe)].from == half) at = ignite.delay + ignite.length;
      }
      both = both && at >= 0.0f;
      reached = std::max(reached, at);
    }
    if (!both) continue;
    const float u = Ease((a - reached) / 0.35f);
    if (u <= 0.0f) continue;
    const glm::vec2 p = frame.At(static_cast<int>(n)), q = frame.At(twin), mid = (p + q) * 0.5f;
    const float alpha = a > reached + 1.5f ? 0.6f : 0.95f;
    canvas.Line(p, p + (mid - p) * u, 2.4f * s, WithAlpha(kIgnite, alpha));
    canvas.Line(q, q + (mid - q) * u, 2.4f * s, WithAlpha(kIgnite, alpha));
  }
}

void SanctumArt::DrawSolved(Canvas& canvas, const ArtFrame& frame) const {
  const Board& board = *frame.board;
  if (!board.Started() || board.Running() || !board.GetOutcome().solved) return;
  const Level& level = *frame.level;
  float t = kSettled;  // since the last phial filled
  for (size_t n = 0; n < level.nodes.size(); ++n) {
    if (level.nodes[n].kind == NodeKind::Cup) t = std::min(t, PaidAge(frame, static_cast<int>(n)));
  }
  const float s = frame.Scale();
  auto lit = [&](float at, float length) { return Ease((t - at) / length); };
  for (size_t i = 0; i < sigils_.size(); ++i) {
    const Sigil& sigil = sigils_[i];
    if (sigil.rings.empty()) continue;
    // A finale's sigils light one after the other, the one by the payoff (its pearl phial) first.
    float shift = 0.0f;
    const Vec2 payoff = level.look.payoff;
    auto far = [&](const Sigil& g) {
      return (g.at.x - payoff.x) * (g.at.x - payoff.x) + (g.at.y - payoff.y) * (g.at.y - payoff.y);
    };
    for (const Sigil& other : sigils_) shift += far(other) < far(sigil) ? kSigilAfter : 0.0f;
    auto lit_sigil = [&](float at, float length) { return Ease((t - at - shift) / length); };
    const glm::vec2 c = frame.layout.ToCanvas(sigil.at);
    const float r = *std::max_element(sigil.rings.begin(), sigil.rings.end()) * s;
    const std::vector<std::vector<Vec2>>& figures = figures_[i];
    // The figure, the inner circle, the counter-figure, the rings from the bottom both ways.
    if (!figures.empty()) {
      const std::vector<glm::vec2> line = Closed(OnCanvas(frame, figures[0]));
      Trace(canvas, line, 0.0f, Length(line) * lit_sigil(1.5f, 0.5f), 1.6f * s, WithAlpha(kIgnite, 0.85f));
    }
    const std::vector<glm::vec2> inner = CircleLine(c, 0.41f * r, kPi * 0.5f, 2.0f * kPi);
    Trace(canvas, inner, 0.0f, Length(inner) * lit_sigil(1.8f, 0.6f), 1.6f * s, WithAlpha(kIgnite, 0.85f));
    if (figures.size() > 1) {
      const std::vector<glm::vec2> line = Closed(OnCanvas(frame, figures[1]));
      Trace(canvas, line, 0.0f, Length(line) * lit_sigil(2.0f, 0.7f), 1.4f * s, WithAlpha(kIgnite, 0.7f));
    }
    for (const float way : {1.0f, -1.0f}) {
      const std::vector<glm::vec2> ring = CircleLine(c, r + 2.0f * s, kPi * 0.5f, way * kPi);
      const float length = Length(ring) * lit_sigil(2.3f, 0.8f);
      Trace(canvas, ring, 0.0f, length, 6.0f * s, WithAlpha(kIgnite, 0.2f));
      Trace(canvas, ring, 0.0f, length, 2.4f * s, WithAlpha(kIgnite, 0.9f));
    }
  }
  // Last, every groove still dark lights from its flask.
  const float rest = lit(2.9f, 0.4f);
  if (rest <= 0.0f) return;
  for (size_t p = 0; p < level.pipes.size(); ++p) {
    if (level.nodes[static_cast<size_t>(level.pipes[p].from)].kind != NodeKind::Spout) continue;
    const std::vector<glm::vec2> line = OnCanvas(frame, grooves_[p]);
    Trace(canvas, line, 0.0f, Length(line) * rest, 2.4f * s, WithAlpha(kIgnite, 0.8f));
  }
}

// ---- The moments ----

bool SanctumArt::DrawMarker(Canvas& canvas, const ArtFrame& frame, const Marker& marker) const {
  if (level_ != frame.level) return false;  // laid out in Prepare, before the markers are drawn
  if (marker.kind == Marker::Jam) {
    DrawJam(canvas, frame, marker);
    // The phials it left short show what they miss.
    for (size_t n = 0; n < frame.level->nodes.size(); ++n) {
      const Node& node = frame.level->nodes[n];
      if (node.kind == NodeKind::Cup && frame.board->Cup(static_cast<int>(n)).count < node.count)
        DrawPhialMoment(canvas, frame, {Marker::Dry, static_cast<int>(n), marker.age});
    }
    return true;
  }
  if (marker.node < 0) return false;
  const NodeKind kind = frame.level->nodes[static_cast<size_t>(marker.node)].kind;
  switch (marker.kind) {
    case Marker::Paid: return true;  // the phial's art shows it (DrawFront)
    case Marker::Mud:
      if (kind == NodeKind::Cup) return true;  // the phial's sludge and its red pill (DrawPhial, DrawPill)
      DrawSludge(canvas, frame, marker);
      return true;
    case Marker::WrongCup:
    case Marker::Spill:
    case Marker::Dry:
      if (kind != NodeKind::Cup) return false;
      DrawPhialMoment(canvas, frame, marker);
      return true;
    default: return false;  // a near miss: the shared look
  }
}

void SanctumArt::DrawSludge(Canvas& canvas, const ArtFrame& frame, const Marker& marker) const {
  const float s = frame.Scale(), age = frame.calm ? kSettled : marker.age;
  const Level& level = *frame.level;
  const Node& node = level.nodes[static_cast<size_t>(marker.node)];
  const glm::vec2 at = frame.At(marker.node);
  // The stain runs out along every groove that meets the globe.
  const float reach = (6.0f + 40.0f * EaseOut(Progress(frame, marker, kSludgeLength))) * s;
  auto stain = [&](int pipe, bool leaving) {
    std::vector<glm::vec2> line = OnCanvas(frame, grooves_[static_cast<size_t>(pipe)]);
    if (!leaving) std::reverse(line.begin(), line.end());
    Trace(canvas, line, 0.0f, reach, 12.0f * s, WithAlpha(kSludge, 0.95f));
    for (float d = 0.0f; d < reach; d += 6.5f * s)
      Trace(canvas, line, d, std::min(d + 1.5f * s, reach), 1.4f * s, WithAlpha(kSludgeDash, 0.8f));
  };
  for (const int pipe : node.ins) stain(pipe, false);
  for (const int pipe : node.outs) stain(pipe, true);
  // The cloud, the clotted globe, bubbles.
  const float r = (18.0f + 34.0f * EaseOut(std::min(1.0f, age / 1.2f))) * s;
  for (int k = 0; k < 5; ++k) {
    const float angle = 2.0f * kPi * static_cast<float>(k) / 5.0f;
    SoftDisc(canvas, at + glm::vec2(std::cos(angle), std::sin(angle)) * (r * 0.4f), r * 0.75f, WithAlpha(kCloud, 0.3f));
  }
  SoftDisc(canvas, at, r * 0.8f, WithAlpha(kCloudDark, 0.5f));
  canvas.Circle(at, 13.0f * s, WithAlpha(kClot, 0.85f * std::min(1.0f, age / 0.6f)));
  for (int j = 0; j < 5; ++j) {
    const float ph = std::fmod(age * 0.8f + static_cast<float>(j) * 0.37f, 1.0f), k = static_cast<float>(j);
    const glm::vec2 b = at + glm::vec2(std::cos(2.4f * k) * (8.0f + 4.0f * k) * 0.5f,
                                       std::sin(2.4f * k) * (6.0f + 3.0f * k) * 0.5f - 6.0f * ph) *
                                 s;
    canvas.Ring(b, (1.5f + 2.5f * ph) * s, 1.2f * s, WithAlpha(kBubble, 0.8f * (1.0f - ph)));
  }
}

void SanctumArt::DrawCrack(Canvas& canvas, const ArtFrame& frame, int node, float grown) const {
  const float s = frame.Scale();
  const glm::vec2 at = frame.At(node);
  const float k = (globes_[static_cast<size_t>(node)] > 20.0f ? 22.0f : 15.0f) / 15.0f * s;
  const std::vector<glm::vec2> crack{at + glm::vec2(-2, -12) * k, at + glm::vec2(1, -5) * k, at + glm::vec2(-2, 1) * k,
                                     at + glm::vec2(2, 8) * k};
  Trace(canvas, crack, 0.0f, Length(crack) * grown, 1.6f * s, kFail);
  const std::vector<glm::vec2> branch{at + glm::vec2(1, -5) * k, at + glm::vec2(7, -2) * k};
  Trace(canvas, branch, 0.0f, Length(branch) * std::clamp(grown * 2.0f - 1.0f, 0.0f, 1.0f), 1.6f * s, kFail);
}

void SanctumArt::DrawJam(Canvas& canvas, const ArtFrame& frame, const Marker& marker) const {
  const Level& level = *frame.level;
  const float s = frame.Scale(), traced = Progress(frame, marker, kJamLength);
  const float pulse = frame.calm ? 1.0f : 0.5f + 0.5f * std::sin(6.0f * frame.time);
  const JamReport jam = frame.board->Jam();
  const int lonely = jam.lonely;
  if (lonely >= 0) {
    // The lonely seal (or mixer) cracks; the way its partner would come, dashed red, to a flask gone empty.
    DrawCrack(canvas, frame, lonely, Progress(frame, marker, 0.5f));
    glow_.Draw(canvas, frame.At(lonely), 20.0f * s, WithAlpha(kFailGlow, 0.5f + 0.45f * (pulse * 2.0f - 1.0f)));
    int lane = 0;
    for (const Drop& drop : frame.board->Drops()) {
      if (drop.alive && drop.loc.kind == Loc::InNode && drop.loc.index == lonely) lane = std::max(0, drop.lane_in);
    }
    const Color partner = sigil::PartnerColor(level, lonely, lane);
    const std::string text =
        partner == Color::None ? std::string("waits for a partner") : std::string("waits for ") + ColorName(partner);
    const glm::vec2 c = frame.At(lonely);
    const bool right = level.nodes[static_cast<size_t>(lonely)].at.x > 250.0f;
    canvas.Text(*sigil_, text, c + glm::vec2(right ? -26.0f : 26.0f, 32.0f) * s, 10.5f * s,
                right ? Align::Right : Align::Left, TextStyle{.color = WithAlpha(kGhost, 0.7f + 0.3f * pulse)});
    const std::vector<Vec2> route = sigil::PartnerRoute(level, lonely, lane);
    if (route.size() >= 2) {
      const std::vector<glm::vec2> line = OnCanvas(frame, route);
      Dashes(canvas, line, 5.0f * s, 5.0f * s, 0.0f, 2.0f * s,
             WithAlpha(kFail, 0.8f * std::min(1.0f, marker.age / 0.8f + (frame.calm ? 1.0f : 0.0f))));
      for (size_t n = 0; n < level.nodes.size(); ++n) {
        const Node& flask = level.nodes[n];
        if (flask.kind != NodeKind::Spout || !(flask.at == route.front())) continue;
        const sigil::Vessel& v = vessels_[n];
        DashedRing(canvas, frame.layout.ToCanvas(v.centre), (v.r + 5.0f) * s, 2.0f * s,
                   WithAlpha(kEmpty, std::min(1.0f, traced) * (0.6f + 0.4f * pulse)), 4.0f * s, 4.0f * s);
      }
    }
  }
  // A circle of waiting: traced round once, then it marches; each mixer on it cracks.
  const std::vector<Vec2> loop = sigil::JamLoop(level, jam.cycle);
  if (loop.size() >= 2) {
    const std::vector<glm::vec2> line = OnCanvas(frame, loop);
    const float length = Length(line);
    if (traced < 1.0f) {
      Trace(canvas, line, 0.0f, length * traced, 2.4f * s, kFail);
    } else {
      Dashes(canvas, line, 8.0f * s, 6.0f * s, frame.calm ? 0.0f : frame.time * 20.0f * s, 2.4f * s, kFail);
    }
    for (const int node : jam.cycle) {
      if (node != lonely && HasPiece(frame, node, PieceType::Mixer)) DrawCrack(canvas, frame, node, traced);
    }
  }
}

void SanctumArt::DrawPhialMoment(Canvas& canvas, const ArtFrame& frame, const Marker& marker) const {
  const int cup = marker.node;
  const Node& node = frame.level->nodes[static_cast<size_t>(cup)];
  const sigil::Vessel& v = vessels_[static_cast<size_t>(cup)];
  const float s = frame.Scale(), u = Progress(frame, marker, kFailLength);
  const glm::vec2 c = frame.layout.ToCanvas(v.centre);
  const float r = v.r * s;
  switch (marker.kind) {
    case Marker::WrongCup: canvas.Ring(c, r + 2.0f * s, 2.0f * s, WithAlpha(kFail, u)); break;
    case Marker::Dry: {
      // The ghost of what's missing: a dashed outline from its level up to full.
      const float fill =
          std::clamp(frame.cup_fill[static_cast<size_t>(cup)] / static_cast<float>(std::max(1, node.count)), 0.0f,
                     1.0f) *
          0.82f;
      const float bottom = c.y + r - 2.0f * r * fill, top = c.y + r - 2.0f * r * 0.82f;
      const float half = 0.7f * r;
      const Rgba ink = WithAlpha(kGhost, 0.9f * u);
      art::DashedLine(canvas, {c.x - half, top}, {c.x + half, top}, 1.5f * s, ink, 4);
      art::DashedLine(canvas, {c.x + half, top}, {c.x + half, bottom}, 1.5f * s, ink, 3);
      art::DashedLine(canvas, {c.x + half, bottom}, {c.x - half, bottom}, 1.5f * s, ink, 4);
      art::DashedLine(canvas, {c.x - half, bottom}, {c.x - half, top}, 1.5f * s, ink, 3);
      break;
    }
    case Marker::Spill: {
      // It runs over: a trickle down the glass and a puddle spreading on the table.
      const Rgba color = LiquidColor(*frame.palette, node.want);
      const glm::vec2 up = V(v.up);
      const glm::vec2 lip = c + up * (v.r + sigil::kPhialNeck) * s + glm::vec2(6.0f * s, 0.0f);
      const std::vector<glm::vec2> trickle{lip, c + glm::vec2(0.75f * r, -0.65f * r), c + glm::vec2(r + 1.0f * s, 0.0f),
                                           c + glm::vec2(0.7f * r, 0.75f * r)};
      Trace(canvas, trickle, 0.0f, Length(trickle) * EaseOut(std::min(1.0f, u * 2.0f)), 3.0f * s, color);
      const float e = EaseOut(u);
      art::FilledEllipse(canvas, c + glm::vec2(0.4f * r, r + 3.0f * s), {1.4f * r * e, 3.5f * s * e},
                         WithAlpha(color, 0.85f));
      break;
    }
    default: break;
  }
}

float SanctumArt::MomentLength(const Marker& marker) const {
  switch (marker.kind) {
    case Marker::Paid:  // and the lit sigil is seen a moment before the card
      return (level_ && level_->look.height > kBoardSize.y ? sigil::kGrandLength : sigil::kSolvedLength) + kLitHold;
    case Marker::Mud:
      return level_ && marker.node >= 0 && level_->nodes[static_cast<size_t>(marker.node)].kind == NodeKind::Cup
                 ? kPhialSludgeLength
                 : kSludgeLength;
    case Marker::WrongCup:
    case Marker::Spill:
    case Marker::Dry: return kFailLength;
    case Marker::Jam: return kJamLength;
    default: return 0.0f;
  }
}

wlw::ui::Rect SanctumArt::CupPool(const ArtFrame& frame, int cup) const {
  const sigil::Vessel& v = vessels_[static_cast<size_t>(cup)];
  const float h = v.r * 0.7f;
  return {frame.layout.ToCanvas({v.centre.x - h, v.centre.y - h}),
          frame.layout.ToCanvas({v.centre.x + h, v.centre.y + h})};
}

glm::vec2 SanctumArt::CupTop(const ArtFrame& frame, int cup) const {
  const sigil::Vessel& v = vessels_[static_cast<size_t>(cup)];
  const float reach = v.r + sigil::kPhialNeck + 10.0f;
  return frame.layout.ToCanvas({v.centre.x + v.up.x * reach, v.centre.y + v.up.y * reach});
}

void SanctumArt::DrawMud(Canvas& canvas, glm::vec2 at, float age, float scale, float) const {
  const float r = (18.0f + 34.0f * EaseOut(std::min(1.0f, age / 1.2f))) * scale;
  SoftDisc(canvas, at, r, WithAlpha(kCloud, 0.6f));
  canvas.Circle(at, 13.0f * scale, WithAlpha(kClot, 0.85f));
}

float SanctumArt::SocketRadius(const Node& node) const {
  if (level_ && !level_->nodes.empty() && &node >= &level_->nodes.front() && &node <= &level_->nodes.back()) {
    const int n = static_cast<int>(&node - &level_->nodes.front());
    if (TwinOf(*level_, n) >= 0) return 21.0f;
  }
  return node.socket ? 28.0f : 19.0f;
}

}  // namespace threadlock
