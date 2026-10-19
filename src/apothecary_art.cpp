#include "apothecary_art.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <string>

#include "art_shapes.h"
#include "wlw/rendering/shader.h"

namespace threadlock {
namespace {

using art::kPi;
using wlw::ui::Align;
using wlw::ui::Canvas;
using wlw::ui::Rect;
using wlw::ui::TextStyle;

// The shop's woods and papers.
constexpr Rgba kPlankTop = Hex(0x8A6038), kPlankLow = Hex(0x74502E), kPlankEdge = Hex(0x3E2A18);
constexpr Rgba kPlankShine = Hex(0xC9965E, 0.45f), kBracket = Hex(0x2A1A0E);
constexpr Rgba kCounterTop = Hex(0xA47148), kCounterMid = Hex(0x8A5E3A), kCounterLow = Hex(0x6E4A2C);
constexpr Rgba kCounterFront = Hex(0x3A2516), kDoorGroove = Hex(0x2A1A0F), kDoorBead = Hex(0x5A3C24);
constexpr Rgba kCork = Hex(0xB98A55), kCorkFrame = Hex(0x8A6038), kCorkSpeck = Hex(0x8A6A40);
constexpr Rgba kPaper = Hex(0xF3E8CC), kPaperEdge = Hex(0xC9B080), kInkBrown = Hex(0x3A2A1A), kPin = Hex(0xB23A2A);
constexpr Rgba kLabel = Hex(0xD8C8A0);
// The shop's own stock on the shelves: muted, half lost in the wall's shadow.
constexpr Rgba kStock[] = {Hex(0x6C7A5E), Hex(0x5A3E52), Hex(0x7A5A3A), Hex(0x3E5A6A),
                           Hex(0x6A4A3A), Hex(0x4E5E3E), Hex(0x5E4E6A)};
constexpr float kStockAlpha = 0.55f;

// Sizes, in board units.
constexpr float kPlank = 9.0f, kPlankFront = 8.0f;  // a shelf's top face and its front edge
constexpr float kCounterFace = 14.0f;               // the counter's top surface, seen from above

// The copper lines (board units). The liquid runs inside them, seen only through a porthole per cell: the
// copper is drawn over it, cut round each porthole (flat ends) with a rim over the window's corners.
constexpr Rgba kCopperEdge = Hex(0x4A2210), kCopper = Hex(0xB5683A), kCopperShade = Hex(0x8E4A26);
constexpr Rgba kCopperShine = Hex(0xD59060), kCopperRim = Hex(0xE8B48A), kBore = Hex(0x150B06);
constexpr float kPipeHalf = 6.5f;                 // the copper's outer edge
constexpr float kCopperHalf = 5.0f;               // inside its dark edge
constexpr float kHole = 2.8f;                     // a porthole
constexpr float kPipeBend = 14.0f;                // how round the bends are
constexpr float kFitting = 8.0f;                  // half a bend's brass fitting
constexpr float kBoss = 11.0f, kBossHole = 4.0f;  // a crossing's or a merge's copper boss
constexpr float kForkBoss = 9.0f, kForkHole = 3.5f;
constexpr glm::vec2 kLightFrom{-0.45f, -0.89f};  // the shop's light falls from the top left

const char* const kRoman[] = {"I", "II", "III", "IV", "V", "VI", "VII", "VIII"};
constexpr float kSlipTilt[] = {-3.0f, 2.0f, -2.0f, 3.0f, -1.5f, 2.5f};  // degrees, as pinned

uint64_t Mix(uint64_t h, uint64_t v) {
  h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
  return h;
}

uint64_t Bits(float f) {
  uint32_t u = 0;
  std::memcpy(&u, &f, sizeof(u));
  return u;
}

uint32_t HashText(const std::string& text) {
  uint32_t h = 2166136261u;  // FNV-1a
  for (const char c : text) h = (h ^ static_cast<uint8_t>(c)) * 16777619u;
  return h;
}

float Hash01(uint32_t x) {
  x ^= x >> 16;
  x *= 0x7FEB352Du;
  x ^= x >> 15;
  x *= 0x846CA68Bu;
  x ^= x >> 16;
  return static_cast<float>(x & 0xFFFF) / 65535.0f;
}

glm::vec2 Turn(glm::vec2 v, float c, float s) { return {v.x * c - v.y * s, v.x * s + v.y * c}; }

glm::vec2 Unit(glm::vec2 v) {
  const float length = glm::length(v);
  return length > 1e-5f ? v / length : glm::vec2(0.0f, 1.0f);
}

glm::vec2 Left(glm::vec2 d) { return {d.y, -d.x}; }

glm::vec2 V(Vec2 p) { return {p.x, p.y}; }

// How far from a node's centre its copper stops: the boss or the part drawn there covers the rest.
float CopperStop(const Node& node) {
  switch (node.kind) {
    case NodeKind::Spout:
    case NodeKind::Mill:
    case NodeKind::Drain: return node.radius;
    case NodeKind::Fork: return kForkBoss - 1.0f;
    case NodeKind::Cross:
    case NodeKind::Merge: return kBoss - 1.0f;
    case NodeKind::Rack: return 12.0f;
    case NodeKind::Cup:
    case NodeKind::Pass:
    case NodeKind::Gate: return 0.0f;
  }
  return 0.0f;
}

// The porthole into a node (a drop inside it shows there): its radius, 0 = none.
float NodeHole(const Node& node) {
  switch (node.kind) {
    case NodeKind::Pass: return kHole;
    case NodeKind::Fork: return kForkHole;
    case NodeKind::Cross:
    case NodeKind::Merge: return kBossHole;
    default: return 0.0f;
  }
}

// A band from -half to half across a line, opaque, with flat ends. Runs along the axes are rectangles;
// others are quads that overlap their neighbours a little and are drawn as both fans (an anti-aliased edge
// inside the band would let the liquid show through).
void FlatBand(Canvas& canvas, const std::vector<glm::vec2>& points, float half, float overlap, const Rgba& color) {
  for (size_t i = 1; i < points.size(); ++i) {
    const glm::vec2 a = points[i - 1], b = points[i];
    if (std::fabs(a.x - b.x) < 0.01f || std::fabs(a.y - b.y) < 0.01f) {
      const glm::vec2 h = std::fabs(a.x - b.x) < 0.01f ? glm::vec2(half, 0.0f) : glm::vec2(0.0f, half);
      canvas.FillRect({glm::min(a, b) - h, glm::max(a, b) + h}, color);
      continue;
    }
    const glm::vec2 d = Unit(b - a), n = Left(d) * half;
    const glm::vec2 p = a - d * (i > 1 ? overlap : 0.0f), q = b + d * (i + 1 < points.size() ? overlap : 0.0f);
    art::FillConvex(canvas, {p - n, q - n, q + n, p + n}, color);
    art::FillConvex(canvas, {q - n, q + n, p + n, p - n}, color);
  }
}

// A porthole's rim (the window cut for it is square; this covers its corners).
void HoleRim(Canvas& canvas, glm::vec2 c, float hole, float s) {
  const float rim = hole * 1.42f;
  canvas.Ring(c, (hole + rim) * 0.5f * s, (rim - hole) * s, kCopperRim);
  canvas.Arc(c, (rim - 0.3f) * s, 0.7f * s, kPi * 0.4f, kPi * 0.9f, kCopperEdge);
}

// Whether anything of the machine stands in this board rectangle (the shop's stock keeps clear of it).
bool Busy(const Level& level, float x0, float x1, float y0, float y1, const apothecary::Box& cork) {
  for (const Node& node : level.nodes) {
    if (node.at.x > x0 - 24.0f && node.at.x < x1 + 24.0f && node.at.y > y0 - 24.0f && node.at.y < y1 + 30.0f)
      return true;
  }
  return !cork.Empty() && x1 > cork.min.x && x0 < cork.max.x && y1 > cork.min.y && y0 < cork.max.y;
}

}  // namespace

void ApothecaryArt::Create(wlw::app::Application& app) {
  wall_shader_ = wlw::rendering::Shader::FromFile("shaders/apothecary_wall.shader");
  hand_ = wlw::ui::Font::Load("fonts/Caveat-Bold.ttf");
  tiles_.Create(app.Window(), "Apothecary", Hex(0x1A120C));
  AddTile();  // every board needs one: made up front, so the shader compiles in the warm-up
}

void ApothecaryArt::AddTile() {
  tiles_.Add();
  TileArt art;
  art.wall = wlw::rendering::ShaderMaterial::Create(wall_shader_);
  art.vignette = wlw::rendering::ShaderMaterial::Create(wall_shader_);
  art.wall->SetParam("uLayer", 0.0f);
  art.vignette->SetParam("uLayer", 1.0f);
  tile_art_.push_back(std::move(art));
}

void ApothecaryArt::Invalidate() { tiles_.Invalidate(); }

void ApothecaryArt::Release() {
  // Only a tall board has more than one tile.
  tiles_.Shrink(1);
  tile_art_.resize(1);
  Invalidate();
}

// ---- The still picture ----

void ApothecaryArt::DrawBack(Canvas& picture, const ArtFrame& frame) {
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
  key = Mix(key, reinterpret_cast<uintptr_t>(frame.palette));  // the slips write in the liquids' colours
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
}

void ApothecaryArt::DrawStill(int slot, int k, const ArtFrame& whole, glm::vec2 screen, float total) {
  TilePool::Slot& tile = tiles_[slot];
  TileArt& art = tile_art_[static_cast<size_t>(slot)];
  Canvas& canvas = tile.view->GetCanvas();
  // This tile's piece of the board: the layout at scroll 0, moved up by the tiles above it.
  ArtFrame frame = whole;
  frame.layout = whole.layout.Unscrolled();
  frame.layout.origin.y -= static_cast<float>(k) * screen.y;
  tile.view->SetUpdateMode(wlw::scene::Viewport::UpdateMode::Once);

  for (const auto& material : {art.wall, art.vignette}) {
    material->SetParam("uCanvas", screen);
    material->SetParam("uOffset", glm::vec2(0.0f, static_cast<float>(k) * screen.y));
    material->SetParam("uTotal", glm::vec2(screen.x, total));
    material->SetParam("uScreen", screen);
    material->SetParam("uScale", frame.Scale());
  }
  canvas.ShaderRect({{0.0f, 0.0f}, screen}, art.wall);
  for (size_t shelf = 0; shelf < frame.level->look.shelves.size(); ++shelf)
    DrawShelf(canvas, frame, static_cast<int>(shelf), screen.x);
  DrawCounter(canvas, frame, screen.x, total - static_cast<float>(k) * screen.y);
  DrawCorkBoard(canvas, frame);
  DrawBores(canvas, frame);
  canvas.ShaderRect({{0.0f, 0.0f}, screen}, art.vignette);
}

void ApothecaryArt::DrawShelf(Canvas& canvas, const ArtFrame& frame, int shelf, float width) const {
  const Level& level = *frame.level;
  const Layout& layout = frame.layout;
  const float s = frame.Scale();
  const float y = level.look.shelves[static_cast<size_t>(shelf)];
  const float top = layout.ToCanvas({0.0f, y}).y;
  const apothecary::Box cork = apothecary::CorkBoard(static_cast<int>(apothecary::Orders(level).size()));

  // The shop's stock standing on it, wherever the machine leaves room (the same for a level every time).
  const float left = layout.ToBoard({0.0f, 0.0f}).x, right = layout.ToBoard({width, 0.0f}).x;
  uint32_t seed = HashText(level.id) * 31u + static_cast<uint32_t>(shelf) * 7919u;
  float x = left + 6.0f + 14.0f * Hash01(seed++);
  while (x < right - 8.0f) {
    const int kind = static_cast<int>(Hash01(seed++) * 4.0f) % 4;
    const float w = kind == 0 ? 14.0f : kind == 1 ? 26.0f : kind == 2 ? 28.0f : 22.0f;
    const float tall = kind == 0 ? 40.0f : kind == 1 ? 34.0f : kind == 2 ? 24.0f : 32.0f;
    const Rgba color =
        WithAlpha(kStock[static_cast<size_t>(Hash01(seed++) * 6.99f)], kStockAlpha * (0.8f + 0.2f * Hash01(seed++)));
    const bool skip = Hash01(seed++) < 0.18f || Busy(level, x, x + w, y - tall, y, cork);
    if (!skip) {
      auto at = [&](float bx, float by) { return layout.ToCanvas({x + bx, y - by}); };
      switch (kind) {
        case 0:  // a tall bottle
          canvas.FillRect({at(0.0f, tall - 8.0f), at(w, 0.0f)}, color, 5.0f * s);
          canvas.FillRect({at(4.0f, tall), at(w - 4.0f, tall - 9.0f)}, color, 1.5f * s);
          break;
        case 1:  // a round flask
          canvas.Circle(at(w * 0.5f, 13.0f), 13.0f * s, color);
          canvas.FillRect({at(w * 0.5f - 4.0f, tall), at(w * 0.5f + 4.0f, 22.0f)}, color, 1.0f * s);
          break;
        case 2:  // a box
          canvas.FillRect({at(0.0f, tall), at(w, 0.0f)}, color, 2.0f * s);
          canvas.Line(at(2.0f, tall - 2.0f), at(w - 2.0f, tall - 2.0f), 1.2f * s, WithAlpha(kLabel, 0.18f));
          break;
        default:  // a labelled jar
          canvas.FillRect({at(0.0f, tall - 4.0f), at(w, 0.0f)}, color, 4.0f * s);
          canvas.FillRect({at(-1.0f, tall), at(w + 1.0f, tall - 5.0f)}, color, 1.5f * s);
          canvas.FillRect({at(3.0f, tall - 12.0f), at(w - 3.0f, tall - 22.0f)}, WithAlpha(kLabel, kStockAlpha), 0.0f);
          break;
      }
    }
    x += w + 6.0f + 16.0f * Hash01(seed++);
  }

  // The plank: a lit top face, a dark front edge and a soft shadow on the wall under it.
  canvas.FillRect({{0.0f, top}, {width, top + kPlank * 0.5f * s}}, kPlankTop);
  canvas.FillRect({{0.0f, top + kPlank * 0.5f * s}, {width, top + kPlank * s}}, kPlankLow);
  canvas.Line({0.0f, top + 0.8f * s}, {width, top + 0.8f * s}, 1.2f * s, kPlankShine);
  const float front = top + kPlank * s, under = front + kPlankFront * s;
  canvas.FillRect({{0.0f, front}, {width, under}}, kPlankEdge);
  for (int band = 0; band < 3; ++band) {
    canvas.FillRect({{0.0f, under}, {width, under + (4.0f + 5.0f * static_cast<float>(band)) * s}},
                    Rgba{0.0f, 0.0f, 0.0f, 0.12f});
  }
  // Brackets near both ends of the board.
  for (const float bx : {20.0f, 340.0f}) {
    const float dir = bx < 180.0f ? 1.0f : -1.0f;
    const glm::vec2 corner = layout.ToCanvas({bx, y + kPlank + kPlankFront});
    canvas.Triangle(corner, corner + glm::vec2(16.0f * dir, 0.0f) * s, corner + glm::vec2(0.0f, 17.0f) * s, kBracket);
  }
  // Herbs drying under the first shelf.
  if (shelf != 0) return;
  for (int bunch = 0; bunch < 2; ++bunch) {
    const float hx = 150.0f + 80.0f * static_cast<float>(bunch) + 20.0f * Hash01(seed++);
    const float hy = y + kPlank + kPlankFront;
    if (Busy(level, hx - 12.0f, hx + 12.0f, hy, hy + 40.0f, cork)) continue;
    const Rgba stem = bunch == 0 ? Hex(0x6E7A4A, 0.7f) : Hex(0x8A6A4A, 0.7f);
    for (int k = 0; k < 4; ++k) {
      const float spread = (static_cast<float>(k) - 1.5f) * 7.0f;
      const float length = 30.0f + 8.0f * Hash01(seed++);
      const std::vector<glm::vec2> points{layout.ToCanvas({hx, hy}),
                                          layout.ToCanvas({hx + spread * 0.35f, hy + length * 0.5f}),
                                          layout.ToCanvas({hx + spread, hy + length})};
      art::Polyline(canvas, points, 2.0f * s, k % 2 ? Mixed(stem, Hex(0xFFFFFF, stem.a), 0.12f) : stem);
    }
    canvas.FillRect({layout.ToCanvas({hx - 3.0f, hy}), layout.ToCanvas({hx + 3.0f, hy + 4.0f})}, Hex(0xB98A55, 0.8f),
                    1.0f * s);
  }
}

void ApothecaryArt::DrawCounter(Canvas& canvas, const ArtFrame& frame, float width, float bottom) const {
  const Layout& layout = frame.layout;
  const float s = frame.Scale();
  const float y = apothecary::CounterTop(*frame.level);
  const float top = layout.ToCanvas({0.0f, y}).y;
  if (top > bottom) return;
  // Its top surface (seen from above: light to the front edge), then the front down to the floor.
  const float face = kCounterFace * s;
  canvas.FillRect({{0.0f, top}, {width, top + face * 0.4f}}, kCounterTop);
  canvas.FillRect({{0.0f, top + face * 0.4f}, {width, top + face * 0.75f}}, kCounterMid);
  canvas.FillRect({{0.0f, top + face * 0.75f}, {width, top + face}}, kCounterLow);
  canvas.Line({0.0f, top + 0.75f * s}, {width, top + 0.75f * s}, 1.5f * s, Hex(0xE8C8A0, 0.35f));
  canvas.FillRect({{0.0f, top + face}, {width, bottom}}, kCounterFront);
  canvas.FillRect({{0.0f, top + face}, {width, top + face + 3.0f * s}}, Rgba{0.0f, 0.0f, 0.0f, 0.3f});
  // Cupboard doors under it: a groove and a bead round each panel.
  for (int door = 0; door < 3; ++door) {
    const float x0 = 3.0f + 125.0f * static_cast<float>(door);
    const Rect outer{layout.ToCanvas({x0, y + kCounterFace + 17.0f}), layout.ToCanvas({x0 + 104.0f, y + 127.0f})};
    if (outer.min.y > bottom) break;
    auto inset = [&](const Rect& r, float by) { return Rect{r.min + glm::vec2(by * s), r.max - glm::vec2(by * s)}; };
    canvas.FillRect(outer, kDoorGroove, 6.0f * s);
    canvas.FillRect(inset(outer, 3.0f), kCounterFront, 5.0f * s);
    canvas.FillRect(inset(outer, 4.0f), kDoorBead, 4.5f * s);
    canvas.FillRect(inset(outer, 5.2f), kCounterFront, 4.0f * s);
  }
}

void ApothecaryArt::DrawCorkBoard(Canvas& canvas, const ArtFrame& frame) const {
  const std::vector<apothecary::Order> orders = apothecary::Orders(*frame.level);
  const int n = static_cast<int>(orders.size());
  const apothecary::Box box = apothecary::CorkBoard(n);
  if (box.Empty()) return;
  const Layout& layout = frame.layout;
  const float s = frame.Scale();
  const Rect board{layout.ToCanvas(box.min), layout.ToCanvas(box.max)};
  canvas.FillRect({board.min + glm::vec2(0.0f, 4.0f * s), board.max + glm::vec2(2.0f, 6.0f) * s},
                  Rgba{0.0f, 0.0f, 0.0f, 0.3f}, 5.0f * s);
  canvas.FillRect(board, kCorkFrame, 4.0f * s);
  canvas.FillRect({board.min + glm::vec2(3.5f * s), board.max - glm::vec2(3.5f * s)}, kCork, 2.0f * s);
  // Cork's grain: specks.
  uint32_t seed = HashText(frame.level->id) + 77u;
  for (int k = 0; k < 14; ++k) {
    const glm::vec2 p = layout.ToCanvas({box.min.x + 6.0f + (box.max.x - box.min.x - 12.0f) * Hash01(seed++),
                                         box.min.y + 6.0f + (box.max.y - box.min.y - 12.0f) * Hash01(seed++)});
    canvas.Line(p, p + glm::vec2(2.0f * s, 0.0f), 1.6f * s, kCorkSpeck);
  }

  // The slips: one per order, pinned at the top, a little askew; the number of its order, the name and
  // how many drops it wants, in the liquid's colour.
  const wlw::ui::Font& hand = *hand_;
  for (int k = 0; k < n; ++k) {
    const apothecary::Order& order = orders[static_cast<size_t>(k)];
    const glm::vec2 pin = layout.ToCanvas(apothecary::SlipPin(n, k));
    const float tilt = kSlipTilt[static_cast<size_t>(k) % std::size(kSlipTilt)] * kPi / 180.0f;
    const float c = std::cos(tilt), sn = std::sin(tilt);
    auto at = [&](float x, float y) { return pin + Turn(glm::vec2(x, y) * s, c, sn); };
    const float half = apothecary::kSlipWidth * 0.5f;
    const float y0 = -5.0f, y1 = apothecary::kSlipHeight - 5.0f;
    std::vector<glm::vec2> paper{at(-half, y0), at(half, y0), at(half, y1), at(-half, y1)};
    std::vector<glm::vec2> shadow = paper;
    for (glm::vec2& p : shadow) p += glm::vec2(1.2f, 2.2f) * s;
    art::FillConvex(canvas, shadow, Rgba{0.0f, 0.0f, 0.0f, 0.22f});
    art::FillConvex(canvas, paper, kPaper);
    for (size_t i = 0; i < paper.size(); ++i)
      canvas.Line(paper[i], paper[(i + 1) % paper.size()], 0.8f * s, kPaperEdge);

    TextStyle ink{.color = kInkBrown};
    ink.rotation = tilt;
    canvas.Text(hand, kRoman[static_cast<size_t>(k) % std::size(kRoman)], at(0.0f, 9.0f), 13.0f * s, Align::Center,
                ink);
    float name_size = 12.0f * s;
    const float room = (apothecary::kSlipWidth - 4.0f) * s;
    if (const float wide = hand.Measure(order.name, name_size); wide > room) name_size *= room / wide;
    canvas.Text(hand, order.name, at(0.0f, 22.0f), name_size, Align::Center, ink);
    const Rgba liquid = LiquidColor(*frame.palette, order.want);
    TextStyle want{.color = Mixed(liquid, Hex(0x000000), 0.3f)};
    want.rotation = tilt;
    canvas.Text(hand, std::to_string(order.count), at(3.0f, 38.0f), 19.0f * s, Align::Center, want);
    art::Glyph(canvas, order.want, at(-9.0f, 38.5f), 3.2f * s, liquid);

    canvas.Circle(pin + glm::vec2(0.6f, 1.2f) * s, 2.8f * s, Rgba{0.0f, 0.0f, 0.0f, 0.3f});
    canvas.Circle(pin, 2.7f * s, kPin);
    canvas.Circle(pin - glm::vec2(0.8f) * s, 0.9f * s, Hex(0xFFD0C0, 0.8f));
  }
}

// ---- The copper lines ----

void ApothecaryArt::Prepare(const ArtFrame& frame) {
  const Level& level = *frame.level;
  if (level_ == &level && tubes_.size() == level.pipes.size()) return;
  level_ = &level;
  tubes_.clear();
  for (const Pipe& pipe : level.pipes) {
    Tube tube;
    const Node& from = level.nodes[static_cast<size_t>(pipe.from)];
    const Node& to = level.nodes[static_cast<size_t>(pipe.to)];
    std::vector<glm::vec2> points;
    for (Vec2 p : pipe.points) points.push_back(V(p));
    points = art::RoundCorners(points, kPipeBend, 2.5f);
    const float rounded = art::MakePath(points).Length();
    tube.stretch = pipe.length > 0.0f ? rounded / pipe.length : 1.0f;
    std::vector<glm::vec2> whole;
    if (glm::distance(V(from.at), points.front()) > 0.01f) whole.push_back(V(from.at));
    whole.insert(whole.end(), points.begin(), points.end());
    if (glm::distance(V(to.at), points.back()) > 0.01f) whole.push_back(V(to.at));
    tube.whole = art::MakePath(std::move(whole));
    tube.lead = glm::distance(V(from.at), points.front());
    tube.copper = {CopperStop(from), tube.whole.Length() - CopperStop(to)};
    // A brass fitting on each bend, at its corner.
    std::vector<glm::vec2> corners{V(from.at)};
    for (Vec2 p : pipe.via) corners.push_back(V(p));
    corners.push_back(V(to.at));
    for (size_t i = 1; i + 1 < corners.size(); ++i) {
      if (glm::dot(Unit(corners[i] - corners[i - 1]), Unit(corners[i + 1] - corners[i])) < 0.995f)
        tube.bends.push_back(corners[i]);
    }
    // A porthole in the middle of each cell (where a drop rests), unless a fitting or a part covers it.
    for (int k = 0; k < pipe.cells; ++k) {
      const float d =
          tube.lead + (static_cast<float>(k) + 0.5f) * pipe.length / static_cast<float>(pipe.cells) * tube.stretch;
      bool shown = d > tube.copper.x + kHole + 1.0f && d < tube.copper.y - kHole - 1.0f;
      for (const glm::vec2 bend : tube.bends)
        shown = shown && glm::distance(tube.whole.PointAt(d), bend) > kFitting + kHole + 0.5f;
      tube.holes.push_back(shown ? d : -1.0f);
      if (shown) tube.windows.push_back(d);
    }
    if (from.kind == NodeKind::Pass) tube.windows.insert(tube.windows.begin(), 0.0f);
    if (to.kind == NodeKind::Pass) tube.windows.push_back(tube.whole.Length());
    tube.top = tube.bottom = tube.whole.points.front().y;
    for (const glm::vec2 p : tube.whole.points) {
      tube.top = std::min(tube.top, p.y - kFitting);
      tube.bottom = std::max(tube.bottom, p.y + kFitting);
    }
    tubes_.push_back(std::move(tube));
  }
}

glm::vec2 ApothecaryArt::PipePoint(const ArtFrame& frame, int pipe, float distance) const {
  const Pipe& rules = frame.level->pipes[static_cast<size_t>(pipe)];
  if (level_ != frame.level || static_cast<size_t>(pipe) >= tubes_.size() || rules.length <= 0.0f)
    return BoardArt::PipePoint(frame, pipe, distance);
  const Tube& tube = tubes_[static_cast<size_t>(pipe)];
  const glm::vec2 p = tube.whole.PointAt(tube.lead + distance * tube.stretch);
  return frame.layout.ToCanvas({p.x, p.y});
}

void ApothecaryArt::DrawBores(Canvas& canvas, const ArtFrame& frame) const {
  const Level& level = *frame.level;
  const Layout& layout = frame.layout;
  const float s = frame.Scale();
  auto on_canvas = [&](const art::Path& path, glm::vec2 shift) {
    std::vector<glm::vec2> points;
    for (const glm::vec2 p : path.points) points.push_back(layout.ToCanvas({p.x + shift.x, p.y + shift.y}));
    return art::MakePath(std::move(points));
  };
  // Soft shadows on the wall, then the bores: dark, as an empty porthole shows them.
  for (const Tube& tube : tubes_) {
    const art::Path shadow = on_canvas(tube.whole.Part(tube.copper.x, tube.copper.y), {1.0f, 6.0f});
    art::Ribbon(canvas, shadow, -(kPipeHalf + 1.5f) * s, (kPipeHalf + 1.5f) * s, Rgba{0.0f, 0.0f, 0.0f, 0.16f});
    art::Ribbon(canvas, shadow, -(kPipeHalf - 1.5f) * s, (kPipeHalf - 1.5f) * s, Rgba{0.0f, 0.0f, 0.0f, 0.16f});
  }
  for (const Tube& tube : tubes_) {
    const art::Path bore = on_canvas(tube.whole.Part(tube.copper.x, tube.copper.y), {0.0f, 0.0f});
    art::Stroke(canvas, bore, 0.0f, 2.0f * kCopperHalf * s, kBore);
  }
  for (const Node& node : level.nodes) {
    const glm::vec2 c = layout.ToCanvas(node.at);
    if (node.kind == NodeKind::Cross || node.kind == NodeKind::Merge || node.kind == NodeKind::Fork)
      canvas.Circle(c, (node.kind == NodeKind::Fork ? kForkBoss : kBoss) * s, kBore);
    // The workshop's spouts, drains and cups (until their own art, step 3c).
    if (node.kind == NodeKind::Spout) {
      canvas.Circle(c, 26.0f * s, ui_color::kPipeEdge);
      canvas.Circle(c, 22.0f * s, ui_color::kPipeInside);
    }
    if (node.kind == NodeKind::Drain) {
      canvas.Circle(c, 17.0f * s, ui_color::kPipeEdge);
      canvas.Circle(c, 14.0f * s, ui_color::kPipeInside);
    }
    if (node.kind == NodeKind::Cup) {
      canvas.FillRect(
          {layout.ToCanvas({node.at.x - 24.0f, node.at.y}), layout.ToCanvas({node.at.x + 24.0f, node.at.y + 62.0f})},
          WithAlpha(ui_color::kPipeInside, 0.9f), 6.0f * s);
    }
  }
}

void ApothecaryArt::DrawCopper(Canvas& canvas, const ArtFrame& frame, const Tube& tube, float top, float bottom) const {
  const Layout& layout = frame.layout;
  const float s = frame.Scale();
  auto on_canvas = [&](float from, float to) {
    std::vector<glm::vec2> points;
    for (const glm::vec2 p : tube.whole.Part(from, to).points) points.push_back(layout.ToCanvas({p.x, p.y}));
    return points;
  };
  auto shown = [&](float from, float to) {
    const float y0 = tube.whole.PointAt(from).y, y1 = tube.whole.PointAt(to).y;
    return std::max(y0, y1) + kPipeHalf > top && std::min(y0, y1) - kPipeHalf < bottom;
  };
  // Outside the portholes' strip the copper runs whole: its dark edges, the metal, a shine on the side the
  // light comes from and a shade on the other.
  const art::Path run = art::MakePath(on_canvas(tube.copper.x, tube.copper.y));
  for (const float side : {-1.0f, 1.0f}) {
    art::Stroke(canvas, run, side * (kPipeHalf - 0.75f) * s, 1.5f * s, kCopperEdge);
    art::Stroke(canvas, run, side * (kHole + kCopperHalf) * 0.5f * s, (kCopperHalf - kHole + 0.1f) * s, kCopper);
  }
  for (size_t i = 1; i < run.points.size(); ++i) {
    const glm::vec2 a = run.points[i - 1], b = run.points[i];
    const glm::vec2 n = Left(Unit(b - a));
    const glm::vec2 lit = glm::dot(n, kLightFrom) >= 0.0f ? n : -n;
    canvas.Line(a + lit * (3.5f * s), b + lit * (3.5f * s), 1.2f * s, kCopperShine);
    canvas.Line(a - lit * (4.3f * s), b - lit * (4.3f * s), 1.0f * s, kCopperShade);
  }
  // The strip down the middle, between the portholes' windows.
  float at = tube.copper.x;
  auto piece = [&](float to) {
    if (to > at + 0.05f && shown(at, to)) FlatBand(canvas, on_canvas(at, to), kHole * s, 0.6f * s, kCopper);
  };
  for (const float window : tube.windows) {
    piece(window - kHole);
    at = std::max(at, window + kHole);
  }
  piece(tube.copper.y);
  // The portholes' rims.
  for (const float d : tube.holes) {
    if (d < 0.0f || !shown(d, d)) continue;
    const glm::vec2 p = tube.whole.PointAt(d);
    HoleRim(canvas, layout.ToCanvas({p.x, p.y}), kHole, s);
  }
}

void ApothecaryArt::DrawGlints(Canvas& canvas, const ArtFrame& frame) const {
  const Level& level = *frame.level;
  const Board& board = *frame.board;
  if (!board.Started()) return;
  const float s = frame.Scale();
  const float alpha = std::clamp(frame.alpha, 0.0f, 1.0f);
  // A drop glints in the portholes it is between, brighter the nearer it is.
  auto glint = [&](Vec2 at, const Rgba& color, float weight) {
    if (weight <= 0.02f) return;
    glow_.Draw(canvas, frame.layout.ToCanvas(at), 14.0f * s, WithAlpha(color, 0.8f * weight));
  };
  auto node_glint = [&](int n, const Rgba& color, float weight) {
    const Node& node = level.nodes[static_cast<size_t>(n)];
    if (NodeHole(node) > 0.0f) glint(node.at, color, weight);
  };
  // `cell` = where along the pipe, in cells (cell k's porthole at k): < 0 is still in the node it left,
  // > cells - 1 already in the next.
  auto along = [&](int p, float cell, const Rgba& color) {
    const Pipe& pipe = level.pipes[static_cast<size_t>(p)];
    const Tube& tube = tubes_[static_cast<size_t>(p)];
    const int k0 = static_cast<int>(std::floor(cell));
    const float t = cell - static_cast<float>(k0);
    for (const int k : {k0, k0 + 1}) {
      const float weight = k == k0 ? 1.0f - t : t;
      if (k < 0) {
        node_glint(pipe.from, color, weight);
      } else if (k >= pipe.cells) {
        node_glint(pipe.to, color, weight);
      } else if (const float d = tube.holes[static_cast<size_t>(k)]; d >= 0.0f) {
        const glm::vec2 q = tube.whole.PointAt(d);
        glint({q.x, q.y}, color, weight);
      }
    }
  };
  for (const Drop& drop : board.Drops()) {
    if (!drop.alive) continue;
    const Rgba a = LiquidColor(*frame.palette, drop.bead.a);
    const Rgba color = drop.bead.IsBlend() ? Mixed(a, LiquidColor(*frame.palette, drop.bead.b), 0.5f) : a;
    const Loc& now = drop.loc;
    const Loc& before = drop.prev;
    if (now.kind == Loc::OnPipe) {
      const float cell = before.kind == Loc::OnPipe && before.index == now.index
                             ? static_cast<float>(before.cell) + static_cast<float>(now.cell - before.cell) * alpha
                             : static_cast<float>(now.cell) - 1.0f + alpha;  // coming out of a node
      along(now.index, cell, color);
    } else if (now.kind == Loc::InNode) {
      if (before.kind == Loc::OnPipe) {
        along(before.index, static_cast<float>(before.cell) + alpha, color);  // going in
      } else {
        node_glint(now.index, color, 1.0f);
      }
    }
  }
}

// ---- The machine (spouts, cups and machine parts are the workshop look's until step 3c) ----

void ApothecaryArt::DrawFront(Canvas& canvas, const ArtFrame& frame) {
  Prepare(frame);
  const Level& level = *frame.level;
  const Layout& layout = frame.layout;
  const float s = frame.Scale();
  // Only what's on screen (board units).
  const float top = layout.ToBoard({0.0f, 0.0f}).y - kFitting;
  const float bottom = layout.ToBoard(canvas.Size()).y + kFitting;
  for (const Tube& tube : tubes_) {
    if (tube.bottom > top && tube.top < bottom) DrawCopper(canvas, frame, tube, top, bottom);
  }
  // Brass fittings on the bends.
  for (const Tube& tube : tubes_) {
    for (const glm::vec2 bend : tube.bends) {
      const glm::vec2 c = layout.ToCanvas({bend.x, bend.y});
      const glm::vec2 h{kFitting * s};
      canvas.FillRect({c - h + glm::vec2(0.0f, 1.5f * s), c + h + glm::vec2(0.0f, 1.5f * s)},
                      Rgba{0.0f, 0.0f, 0.0f, 0.3f}, 4.0f * s);
      canvas.FillRect({c - h, c + h}, art::brass::kDark, 4.0f * s);
      canvas.FillRect({c - h, c + glm::vec2(h.x, h.y - 2.0f * s)}, art::brass::kMid, 4.0f * s);
      canvas.FillRect({c - h + glm::vec2(2.5f * s, 1.0f * s), c + glm::vec2(h.x - 2.5f * s, -h.y + 3.0f * s)},
                      WithAlpha(art::brass::kLight, 0.8f), 1.0f * s);
      canvas.Circle(c + glm::vec2(0.0f, 1.0f * s), 2.2f * s, WithAlpha(art::brass::kShade, 0.8f));
    }
  }
  // A copper boss on every junction, with a porthole into it; a pass socket's porthole on its pipe.
  for (const Node& node : level.nodes) {
    const float hole = NodeHole(node);
    if (hole <= 0.0f) continue;
    const glm::vec2 c = layout.ToCanvas(node.at);
    if (c.y < -40.0f * s || c.y > canvas.Size().y + 40.0f * s) continue;
    if (node.kind != NodeKind::Pass) {
      const float r = node.kind == NodeKind::Fork ? kForkBoss : kBoss;
      canvas.Circle(c + glm::vec2(0.0f, 2.0f * s), (r + 1.0f) * s, Rgba{0.0f, 0.0f, 0.0f, 0.3f});
      canvas.Ring(c, (r + hole) * 0.5f * s, (r - hole) * s, kCopper);
      canvas.Ring(c, (r - 0.7f) * s, 1.4f * s, kCopperEdge);
      canvas.Arc(c, (r - 2.4f) * s, 1.6f * s, -kPi * 0.62f, kPi * 0.6f, kCopperShine);
      canvas.Arc(c, (r - 2.2f) * s, 1.4f * s, kPi * 0.45f, kPi * 0.6f, kCopperShade);
      for (int k = 0; k < 4; ++k) {
        const float a = kPi * (0.25f + 0.5f * static_cast<float>(k));
        canvas.Circle(c + glm::vec2(std::cos(a), std::sin(a)) * ((r + hole) * 0.5f * s), 0.9f * s, kCopperEdge);
      }
    }
    HoleRim(canvas, c, hole, s);
  }
  DrawGlints(canvas, frame);
  parts_.DrawFront(canvas, frame);
}

wlw::ui::Rect ApothecaryArt::CupPool(const ArtFrame& frame, int cup) const { return parts_.CupPool(frame, cup); }

void ApothecaryArt::SpoutLiquid(Liquid& liquid, const ArtFrame& frame, int spout) const {
  parts_.SpoutLiquid(liquid, frame, spout);
}

glm::vec2 ApothecaryArt::CupTop(const ArtFrame& frame, int cup) const { return parts_.CupTop(frame, cup); }

void ApothecaryArt::DrawMud(Canvas& canvas, glm::vec2 at, float age, float scale, float time) const {
  parts_.DrawMud(canvas, at, age, scale, time);
}

}  // namespace threadlock
