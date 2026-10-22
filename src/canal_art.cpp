#include "canal_art.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <utility>

#include "wlw/rendering/shader.h"

namespace threadlock {
namespace {

using art::kPi;
using wlw::ui::Align;
using wlw::ui::Canvas;
using wlw::ui::Rect;
using wlw::ui::TextStyle;

// The city at night: slate rooftops, a dark park, skylights and quay lamps.
constexpr Rgba kRoofTones[] = {Hex(0x161E33), Hex(0x1A233C), Hex(0x1D2742), Hex(0x141B2E), Hex(0x1F2A47)};
constexpr Rgba kRoofShade = Hex(0x000000, 0.22f), kRidge = Hex(0x2C3858), kChimney = Hex(0x0D1322);
constexpr Rgba kSkylight = Hex(0xFFD58A, 0.5f);
constexpr Rgba kTreeDark = Hex(0x10291C), kTreeLight = Hex(0x1A3D29);
constexpr Rgba kLampLight = Hex(0xFFE2A8), kLampGlow = Hex(0xFFC870, 0.18f);
// The canals: a stone bank, a quay inside it, the water; the current's faint dashes.
constexpr Rgba kBank = Hex(0x2A3550), kQuay = Hex(0x3B4A6C), kWater = Hex(0x0B1F35);
constexpr float kBankWidth = 28.0f, kQuayWidth = 23.0f, kWaterWidth = 19.0f;
constexpr Rgba kFlow = Hex(0x1D4A70);
constexpr Rgba kBollard = Hex(0x55658A);
constexpr Rgba kHarbourLamp = Hex(0x6A5A3A);  // dark until its district is served
constexpr Rgba kLeaf = Hex(0xC9974E);         // a lock's brass gates
constexpr float kJunction = 14.0f;            // half a junction's open basin
// A barge: its wake, its tail light, and mud's murk (a barge of mud is the murky colours, dim).
constexpr Rgba kWake = Hex(0x9FD4FF, 0.55f), kTail = Hex(0xFF4D4D);
constexpr Rgba kMurkHull = Hex(0x6A5D3C), kMurkCore = Hex(0x857652), kMurkGlow = Hex(0x4E4429);
constexpr Rgba kPillBack = Hex(0x080E1A, 0.9f), kPillNamed = Hex(0xB9C6E2), kPillFull = Hex(0xB8FAF2);
constexpr float kBargeLength = 11.0f, kBargeWidth = 7.0f;  // board units (a barge's hull)
constexpr float kPillWidth = 92.0f, kPillHeight = 18.0f;   // the harbour pill (board units)
// The mill: a house on the bank away from its rod, a wheel across the canal (18 slats to a turn, one notch a
// barge), a window, the rods with knuckles at their bends and their names along the first run.
constexpr Rgba kHouse = Hex(0x1E2944), kNotchLit = Hex(0xFFC53D), kWindow = Hex(0xFFB347);
constexpr Rgba kWheelDisk = Hex(0x2A1E12), kWheelRim = Hex(0x5A4126), kSlat = Hex(0x8A6A40);
constexpr Rgba kCasing = Hex(0x3A2A12), kKnuckle = Hex(0x5A3E16), kRodDash = Hex(0xFFE6A6);
constexpr Rgba kLabel = Hex(0xB9C6E2, 0.8f), kPulse = Hex(0xFFE6A6);
constexpr float kWheelAlong = 9.0f, kWheelAcross = 17.0f;  // the wheel's half sizes along the canal and across the rod
constexpr int kSlats = 18, kRimSteps = 36;
constexpr float kNotchAcross = 3.5f;  // a notch: across the house face, 8 deep
constexpr std::array<const char*, 3> kStreetNames{"Mill Lane", "Wheel Lane", "Gear Street"};
// The gate: its leaves (14.5 long) hinged 13 either side of the canal, meeting a little upstream when shut; its
// lamp at (12, 28) on a post. Red shut, green open.
constexpr float kGateLeaf = 14.5f, kGateHinge = 13.0f;
constexpr Rgba kHinge = Hex(0x8A6A40), kPost = Hex(0x55658A), kLampRed = Hex(0xFF4D4D), kLampGreen = Hex(0x5BE08A);
// The payoff: a served harbour's lamps (lit), its warm glow, and the district's light: a ring from kDistrictNear out
// to kDistrictFar (board units), the roofs nearest the harbour staying lit at kDistrictSteady.
constexpr Rgba kLampLit = Hex(0xFFF0C8), kLampGlowLit = Hex(0xFFC870), kWarm = Hex(0xFFB347);
constexpr float kDistrictNear = 40.0f, kDistrictFar = 340.0f, kDistrictRing = 30.0f, kDistrictSteady = 0.5f;
constexpr float kSettled = 100.0f;  // seconds: long past any moment (Settle's own figure)
// The failures' moments (seconds): the murk's cloud grows, its stain runs out along the canals, the short's ghost
// berths fade in, the wrong colour and spill tags come up, the jam's circle is traced round once.
constexpr float kMurkLength = 1.2f, kStainLength = 1.4f, kFailLength = 1.2f, kShortLength = 1.2f, kJamLength = 1.3f;
constexpr Rgba kFail = Hex(0xFF5A5A), kFailInk = Hex(0x080E1A);  // the red tags, lamps and the circle
constexpr Rgba kMurkCloud = Hex(0x5E5233), kMurkCloudDark = Hex(0x433A24);
constexpr Rgba kStain = Hex(0x4E4429), kStainDash = Hex(0x6E6040), kGhost = Hex(0xFF8A8A);
constexpr float kStainWidth = 17.0f, kCircleWidth = 2.4f;

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

Rect ToCanvas(const Layout& layout, const canal::Box& box) {
  return {layout.ToCanvas(box.min), layout.ToCanvas(box.max)};
}

// A barge's light, a little paler than the liquid's colour (the lit hull).
Rgba Hull(const Palette& palette, Color color) { return Mixed(LiquidColor(palette, color), Hex(0xFFFFFF), 0.12f); }

// Seconds since a harbour's order was filled (its moment); settled (a long time) when there is none, or calm.
float PaidAge(const ArtFrame& frame, int cup) {
  if (frame.calm) return kSettled;
  for (const Marker& marker : frame.markers) {
    if (marker.kind == Marker::Paid && marker.node == cup) return marker.age;
  }
  return kSettled;
}

// A harbour serves its order: full, pure, and no barge more went over.
bool Served(const ArtFrame& frame, int cup) {
  if (!frame.board->Started()) return false;
  const CupState state = frame.board->Cup(cup);
  return state.count >= frame.level->nodes[static_cast<size_t>(cup)].count && !state.impure && !state.spilled;
}

float EaseOut(float t) { return 1.0f - (1.0f - t) * (1.0f - t); }

// A moment's progress, 0..1 over `length` seconds (a calm frame shows its end at once).
float Progress(const ArtFrame& frame, const Marker& marker, float length) {
  return frame.calm ? 1.0f : std::clamp(marker.age / length, 0.0f, 1.0f);
}

// Whether a moment of this kind happened anywhere on the board.
bool Happened(const ArtFrame& frame, Marker::Kind kind) {
  for (const Marker& marker : frame.markers) {
    if (marker.kind == kind) return true;
  }
  return false;
}

// The moment of this kind at this node, if it happened.
const Marker* MomentOf(const ArtFrame& frame, Marker::Kind kind, int node) {
  for (const Marker& marker : frame.markers) {
    if (marker.kind == kind && marker.node == node) return &marker;
  }
  return nullptr;
}

// A failure's word in a red tag (a pill) at `at`, fading in with alpha 0..1.
void Tag(Canvas& canvas, const ArtFrame& frame, glm::vec2 at, const char* text, float alpha) {
  const float s = frame.Scale();
  const float width = frame.font->Measure(text, 13.0f * s) + 16.0f * s;
  const glm::vec2 half{width * 0.5f, 9.0f * s};
  canvas.FillRect({at - half, at + half}, WithAlpha(kFail, alpha), 9.0f * s);
  canvas.Text(*frame.font, text, at, 13.0f * s, Align::Center, TextStyle{.color = WithAlpha(kFailInk, alpha)});
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

// A point on a mill's wheel at angle `theta`, `reach` of the way from its middle to its rim (canvas units).
glm::vec2 OnWheel(const Layout& layout, const canal::Frame& f, float theta, float reach) {
  return layout.ToCanvas(f.Point(kWheelAlong * reach * std::sin(theta), kWheelAcross * reach * std::cos(theta)));
}

}  // namespace

void CanalArt::Create(wlw::app::Application& app, std::shared_ptr<wlw::ui::Font> street) {
  ground_shader_ = wlw::rendering::Shader::FromFile("shaders/canal_night.shader");
  street_ = std::move(street);
  tiles_.Create(app.Window(), "Canal", Hex(0x0A101B));
  AddTile();  // every board needs one: made up front, so the shader compiles in the warm-up
}

void CanalArt::AddTile() {
  tiles_.Add();
  TileArt art;
  art.ground = wlw::rendering::ShaderMaterial::Create(ground_shader_);
  art.vignette = wlw::rendering::ShaderMaterial::Create(ground_shader_);
  art.ground->SetParam("uLayer", 0.0f);
  art.vignette->SetParam("uLayer", 1.0f);
  tile_art_.push_back(std::move(art));
}

void CanalArt::Invalidate() { tiles_.Invalidate(); }

void CanalArt::Release() {
  // Only a tall board has more than one tile.
  tiles_.Shrink(1);
  tile_art_.resize(1);
  Invalidate();
}

void CanalArt::Prepare(const ArtFrame& frame) {
  const Level& level = *frame.level;
  if (level_ == &level && ways_.size() == level.pipes.size()) return;
  level_ = &level;
  city_ = canal::MakeCity(level);
  canals_.clear();
  for (const std::vector<Vec2>& canal : canal::CityCanals(level)) {
    std::vector<glm::vec2> points;
    for (const Vec2 p : canal) points.push_back(V(p));
    canals_.push_back(art::MakePath(art::RoundCorners(points, canal::kBendRadius, 2.5f)));
  }
  // The drops follow the canal's middle (canal::Ways: the rules' points with the same round bends).
  ways_ = canal::Ways(level);
  locks_.assign(level.nodes.size(), {});
  harbours_.assign(level.nodes.size(), {});
  mills_.assign(level.nodes.size(), {});
  gates_.assign(level.nodes.size(), {});
  for (size_t n = 0; n < level.nodes.size(); ++n) {
    if (level.nodes[n].kind == NodeKind::Spout) locks_[n] = canal::LockAt(level, static_cast<int>(n));
    if (level.nodes[n].kind == NodeKind::Cup) harbours_[n] = canal::HarbourAt(level, static_cast<int>(n));
    if (level.nodes[n].kind == NodeKind::Mill) mills_[n] = canal::MillAt(level, static_cast<int>(n));
    if (level.nodes[n].kind == NodeKind::Gate) gates_[n] = canal::GateAt(level, static_cast<int>(n));
  }
}

// ---- The still picture ----

void CanalArt::DrawBack(Canvas& picture, const ArtFrame& frame) {
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

  DrawFlow(picture, frame);
  for (size_t n = 0; n < frame.level->nodes.size(); ++n) {
    if (frame.level->nodes[n].kind == NodeKind::Spout) DrawMoored(picture, frame, static_cast<int>(n));
  }
}

void CanalArt::DrawStill(int slot, int k, const ArtFrame& whole, glm::vec2 screen, float total) {
  TilePool::Slot& tile = tiles_[slot];
  TileArt& art = tile_art_[static_cast<size_t>(slot)];
  Canvas& canvas = tile.view->GetCanvas();
  // This tile's piece of the board: the layout at scroll 0, moved up by the tiles above it.
  ArtFrame frame = whole;
  frame.layout = whole.layout.Unscrolled();
  frame.layout.origin.y -= static_cast<float>(k) * screen.y;
  tile.view->SetUpdateMode(wlw::scene::Viewport::UpdateMode::Once);

  for (const auto& material : {art.ground, art.vignette}) {
    material->SetParam("uCanvas", screen);
    material->SetParam("uOffset", glm::vec2(0.0f, static_cast<float>(k) * screen.y));
    material->SetParam("uTotal", glm::vec2(screen.x, total));
    material->SetParam("uScreen", screen);
    material->SetParam("uScale", frame.Scale());
  }
  canvas.ShaderRect({{0.0f, 0.0f}, screen}, art.ground);
  // What this tile shows of the board (board units), with a margin for what reaches in from outside it.
  const float top = frame.layout.ToBoard({0.0f, 0.0f}).y - 20.0f;
  const float bottom = frame.layout.ToBoard(screen).y + 20.0f;
  DrawCity(canvas, frame, top, bottom);
  DrawCanals(canvas, frame);
  const Level& level = *frame.level;
  int mills = 0;
  for (size_t n = 0; n < level.nodes.size(); ++n) {
    if (level.nodes[n].kind == NodeKind::Spout) DrawLock(canvas, frame, static_cast<int>(n));
    if (level.nodes[n].kind == NodeKind::Cup) DrawHarbour(canvas, frame, static_cast<int>(n));
    if (level.nodes[n].kind == NodeKind::Mill) DrawMill(canvas, frame, static_cast<int>(n), mills++);
    if (level.nodes[n].kind == NodeKind::Gate) DrawGate(canvas, frame, static_cast<int>(n));
  }
  canvas.ShaderRect({{0.0f, 0.0f}, screen}, art.vignette);
}

void CanalArt::DrawCity(Canvas& canvas, const ArtFrame& frame, float top, float bottom) const {
  const Layout& layout = frame.layout;
  const float s = frame.Scale();
  for (const canal::Roof& roof : city_.roofs) {
    if (roof.box.max.y < top || roof.box.min.y > bottom) continue;
    const Rect r = ToCanvas(layout, roof.box);
    canvas.FillRect(r, kRoofTones[roof.tone]);
    // The half away from the moon in shade, a ridge along the middle, a chimney, a skylight or two.
    const glm::vec2 mid = (r.min + r.max) * 0.5f;
    if (roof.across) {
      canvas.FillRect({{r.min.x, mid.y}, r.max}, kRoofShade);
      canvas.Line({r.min.x + 2.0f * s, mid.y}, {r.max.x - 2.0f * s, mid.y}, 1.0f * s, kRidge);
    } else {
      canvas.FillRect({{mid.x, r.min.y}, r.max}, kRoofShade);
      canvas.Line({mid.x, r.min.y + 2.0f * s}, {mid.x, r.max.y - 2.0f * s}, 1.0f * s, kRidge);
    }
    const glm::vec2 chimney = layout.ToCanvas(roof.chimney);
    canvas.FillRect({chimney, chimney + glm::vec2(3.0f * s)}, kChimney);
    for (const Vec2 window : roof.windows) {
      const glm::vec2 w = layout.ToCanvas(window);
      canvas.FillRect({w, w + glm::vec2(1.8f * s)}, kSkylight);
    }
  }
  for (const canal::Tree& tree : city_.trees) {
    const glm::vec2 c = layout.ToCanvas(tree.at);
    canvas.Circle(c, tree.r * s, kTreeDark);
    canvas.Circle(c - glm::vec2(tree.r * 0.3f * s), tree.r * 0.55f * s, kTreeLight);
  }
  for (const Vec2 lamp : city_.lamps) {
    if (lamp.y < top || lamp.y > bottom) continue;
    const glm::vec2 c = layout.ToCanvas(lamp);
    glow_.Draw(canvas, c, 5.0f * s, kLampGlow);
    canvas.Circle(c, 1.4f * s, kLampLight);
  }
}

void CanalArt::DrawCanals(Canvas& canvas, const ArtFrame& frame) const {
  const Layout& layout = frame.layout;
  const float s = frame.Scale();
  std::vector<art::Path> paths;
  for (const art::Path& canal : canals_) {
    std::vector<glm::vec2> points;
    for (const glm::vec2 p : canal.points) points.push_back(layout.ToCanvas({p.x, p.y}));
    paths.push_back(art::MakePath(std::move(points)));
  }
  // Layer by layer over every canal, so where canals meet their water runs together.
  for (const auto& [width, color] : {std::pair{kBankWidth, kBank}, {kQuayWidth, kQuay}, {kWaterWidth, kWater}}) {
    for (const art::Path& path : paths) art::Stroke(canvas, path, 0.0f, width * s, color);
  }
  // A junction opens into a little basin with a bollard at each corner; a crossing with a bridge locked on it
  // is an aqueduct: one canal runs over the other between parapets.
  const Level& level = *frame.level;
  for (size_t n = 0; n < level.nodes.size(); ++n) {
    const Node& node = level.nodes[n];
    if (node.kind != NodeKind::Cross && node.kind != NodeKind::Merge && node.kind != NodeKind::Fork) continue;
    const glm::vec2 c = layout.ToCanvas(node.at);
    canvas.FillRect({c - glm::vec2(kJunction * s), c + glm::vec2(kJunction * s)}, kWater);
    for (const glm::vec2 corner : {glm::vec2(-1, -1), glm::vec2(1, -1), glm::vec2(-1, 1), glm::vec2(1, 1)})
      canvas.Circle(c + corner * (16.0f * s), 2.2f * s, kBollard);
    bool aqueduct = false;
    for (const TrayPiece& piece : level.tray)
      aqueduct = aqueduct || (piece.locked && piece.at == static_cast<int>(n) && piece.piece.type == PieceType::Bridge);
    if (!aqueduct || node.ins.empty()) continue;
    const std::vector<Vec2> in = canal::CanalPoints(level, level.pipes[static_cast<size_t>(node.ins[0])]);
    glm::vec2 u = glm::normalize(V(in.back()) - V(in[in.size() - 2]));
    const glm::vec2 w{-u.y, u.x};
    canvas.FillRect({c - glm::vec2(kJunction * s), c + glm::vec2(kJunction * s)}, kWater);
    for (const float side : {-1.0f, 1.0f}) {
      const glm::vec2 edge = c + w * (side * 12.5f * s);
      canvas.Line(edge - u * (19.0f * s), edge + u * (19.0f * s), 3.0f * s, kQuay);
      canvas.Line(edge - u * (19.0f * s) - w * (side * 1.0f * s), edge + u * (19.0f * s) - w * (side * 1.0f * s),
                  0.8f * s, WithAlpha(kBollard, 0.8f));
    }
  }
}

void CanalArt::DrawLock(Canvas& canvas, const ArtFrame& frame, int spout) const {
  const canal::Lock& lock = locks_[static_cast<size_t>(spout)];
  const float s = frame.Scale();
  const canal::Frame& f = lock.frame;
  canvas.FillRect(ToCanvas(frame.layout, lock.chamber), kBank, 3.0f * s);
  canvas.FillRect(ToCanvas(frame.layout, f.Span(-canal::kLockBack + 3.0f, canal::kLockFront - 3.0f, -12.0f, 12.0f)),
                  kWater, 2.0f * s);
}

void CanalArt::DrawHarbour(Canvas& canvas, const ArtFrame& frame, int cup) const {
  const canal::Harbour& harbour = harbours_[static_cast<size_t>(cup)];
  const Layout& layout = frame.layout;
  const float s = frame.Scale();
  const canal::Frame& f = harbour.frame;
  const float deep = harbour.Deep();
  canvas.FillRect(ToCanvas(layout, harbour.basin), kBank, 8.0f * s);
  canvas.FillRect(ToCanvas(layout, f.Span(-canal::kBasinBack + 4.0f, deep - 4.0f, -canal::kBasinHalf + 4.0f,
                                          canal::kBasinHalf - 4.0f)),
                  kWater, 6.0f * s);
  // Piers between the last row's berths, and the harbour's four lamps (dark until it's served).
  const float last = canal::kBerthRow * static_cast<float>(harbour.rows - 1);
  for (int q = 0; q <= canal::kBerthsPerRow; ++q) {
    const float across = (static_cast<float>(q) - 3.0f) * 13.0f;
    canvas.Line(layout.ToCanvas(f.Point(36.0f + last, across)), layout.ToCanvas(f.Point(54.0f + last, across)),
                2.0f * s, kQuay);
  }
  for (const float along : {-16.0f, deep + 4.0f}) {
    for (const float across : {-40.0f, 40.0f})
      canvas.Circle(layout.ToCanvas(f.Point(along, across)), 1.8f * s, kHarbourLamp);
  }
}

void CanalArt::DrawMill(Canvas& canvas, const ArtFrame& frame, int n, int ordinal) const {
  const canal::Mill& mill = mills_[static_cast<size_t>(n)];
  const canal::Frame& f = mill.frame;
  const Layout& layout = frame.layout;
  const float s = frame.Scale();
  const Node& node = frame.level->nodes[static_cast<size_t>(n)];
  // The house on the bank away from its rod, shaded on the half away from the moon; six notches on its face.
  canvas.FillRect(ToCanvas(layout, mill.house), kHouse, 2.0f * s);
  canvas.FillRect(ToCanvas(layout, f.Span(0.0f, 22.0f, -56.0f, -20.0f)), kRoofShade, 2.0f * s);
  canvas.Line(layout.ToCanvas(f.Point(-22.0f, -38.0f)), layout.ToCanvas(f.Point(22.0f, -38.0f)), 1.0f * s, kRidge);
  for (int j = 0; j < 6; ++j) {
    const float v = -53.0f + 5.2f * static_cast<float>(j);
    canvas.FillRect(ToCanvas(layout, f.Span(-8.0f, 0.0f, v - kNotchAcross * 0.5f, v + kNotchAcross * 0.5f)), kRidge,
                    1.0f * s);
  }
  canvas.Circle(layout.ToCanvas(f.Point(12.0f, -50.0f)), 6.0f * s, kChimney);  // the window's frame, lit each frame
  // The wheel: its disk, its rim, the axle through it to the hub on the rod's side.
  std::vector<glm::vec2> rim;
  for (int i = 0; i <= kRimSteps; ++i) {
    rim.push_back(OnWheel(layout, f, 2.0f * kPi * static_cast<float>(i) / static_cast<float>(kRimSteps), 1.0f));
  }
  art::FillConvex(canvas, rim, kWheelDisk);
  art::Polyline(canvas, rim, 2.0f * s, kWheelRim);
  canvas.Line(layout.ToCanvas(f.Point(0.0f, -20.0f)), layout.ToCanvas(f.Point(0.0f, 22.0f)), 2.4f * s, kWheelRim);
  canvas.Circle(layout.ToCanvas(f.Point(0.0f, 20.0f)), 3.2f * s, kLeaf);
  // Each rod: its casing, the brass over it with a dash of light, a knuckle at each bend, and its name.
  for (size_t g = 0; g < node.gears.size(); ++g) {
    const std::vector<Vec2> rod = RodPoints(*frame.level, n, static_cast<int>(g));
    std::vector<glm::vec2> points;
    for (const Vec2 p : rod) points.push_back(layout.ToCanvas(p));
    art::Polyline(canvas, points, 5.0f * s, kCasing);
    art::Polyline(canvas, points, 2.6f * s, kLeaf);
    for (size_t k = 1; k + 1 < points.size(); ++k) {
      canvas.FillRect({points[k] - glm::vec2(5.0f * s), points[k] + glm::vec2(5.0f * s)}, kKnuckle, 2.0f * s);
    }
    for (size_t k = 0; k + 1 < points.size(); ++k) {
      const glm::vec2 a = points[k], b = points[k + 1];
      const int dashes = std::max(2, static_cast<int>(glm::length(b - a) / (7.0f * s)));
      art::DashedLine(canvas, a, b, 1.0f * s, WithAlpha(kRodDash, 0.6f), dashes);
    }
    // The name lies along the rod's first run, on its far side, turned to read left to right.
    const glm::vec2 run = glm::normalize(points[1] - points[0]);
    const glm::vec2 mid = (points[0] + points[1]) * 0.5f + glm::vec2(run.y, -run.x) * (-9.0f * s);
    TextStyle style;
    style.color = kLabel;
    style.rotation = std::atan2(run.y, run.x) + (run.x < 0.0f ? kPi : 0.0f);
    canvas.Text(*street_, kStreetNames[static_cast<size_t>(ordinal + static_cast<int>(g)) % kStreetNames.size()], mid,
                9.0f * s, Align::Center, style);
  }
}

void CanalArt::DrawGate(Canvas& canvas, const ArtFrame& frame, int n) const {
  const canal::Gate& gate = gates_[static_cast<size_t>(n)];
  const canal::Frame& f = gate.frame;
  const Layout& layout = frame.layout;
  const float s = frame.Scale();
  for (const float side : {-1.0f, 1.0f})
    canvas.Circle(layout.ToCanvas(f.Point(0.0f, kGateHinge * side)), 2.4f * s, kHinge);
  canvas.Line(layout.ToCanvas(f.Point(12.0f, 14.0f)), layout.ToCanvas(gate.lamp), 2.0f * s, kPost);
  canvas.Circle(layout.ToCanvas(gate.lamp), 3.4f * s, kPost);
}

// ---- Each frame ----

void CanalArt::DrawFlow(Canvas& picture, const ArtFrame& frame) const {
  // Two strands of dashes, one each side of the middle, drifting with the current at their own pace.
  const float s = frame.Scale();
  const float top = frame.layout.ToBoard({0.0f, 0.0f}).y - 10.0f;
  const float bottom = frame.layout.ToBoard(picture.Size()).y + 10.0f;
  struct Strand {
    float across, dash, period, speed, alpha;
  };
  constexpr Strand kStrands[] = {{-4.0f, 3.0f, 14.0f, 6.0f, 0.8f}, {4.0f, 5.0f, 18.0f, 4.4f, 0.6f}};
  for (const art::Path& canal : canals_) {
    for (const Strand& strand : kStrands) {
      const float shift = std::fmod(frame.time * strand.speed, strand.period);
      for (float d = shift; d + strand.dash < canal.Length(); d += strand.period) {
        const glm::vec2 a = canal.PointAt(d), b = canal.PointAt(d + strand.dash);
        if (std::max(a.y, b.y) < top || std::min(a.y, b.y) > bottom) continue;
        const glm::vec2 w = canal.DirectionAt(d);
        const glm::vec2 off = glm::vec2(-w.y, w.x) * strand.across;
        picture.Line(frame.layout.ToCanvas({a.x + off.x, a.y + off.y}),
                     frame.layout.ToCanvas({b.x + off.x, b.y + off.y}), 1.2f * s, WithAlpha(kFlow, strand.alpha));
      }
    }
  }
}

void CanalArt::DrawMoored(Canvas& canvas, const ArtFrame& frame, int spout) const {
  const Node& node = frame.level->nodes[static_cast<size_t>(spout)];
  const canal::Frame& f = locks_[static_cast<size_t>(spout)].frame;
  const int poured = frame.board->Started() ? frame.board->Poured(spout) : 0;
  const int left = static_cast<int>(node.beads.size()) - poured;
  for (int k = 0; k < left; ++k) {
    // Two abreast, the next out at the front nearest the gates; past the chamber they wait up the canal.
    const float along = 6.5f - 12.5f * static_cast<float>(k / 2);
    const float across = k % 2 == 0 ? 4.5f : -4.5f;
    const barges::Pose pose{f.Point(along, across), {f.u.x, f.u.y}};
    DrawBarge(canvas, frame, pose, node.beads[static_cast<size_t>(poured + k)], {0.55f, false});
  }
}

void CanalArt::DrawDrops(Canvas& picture, const ArtFrame& frame) {
  Prepare(frame);
  // After murk, its barges sit dim with no wake; after a jam, the waiting ones' red tails pulse.
  const bool murk = Happened(frame, Marker::Mud), jam = Happened(frame, Marker::Jam);
  const float pulse = 0.55f + 0.45f * std::sin(7.0f * frame.time);
  if (frame.board->Started()) {
    for (const Drop& drop : frame.board->Drops()) {
      if (!drop.alive) continue;
      const barges::Pose pose = barges::PoseOf(*frame.level, ways_, drop, frame.alpha);
      BargeLook look{pose.fade, true};
      if (murk && drop.bead.IsMud()) look = {0.55f, false};
      if (jam && drop.waiting) look.tail_light = pulse;
      DrawBarge(picture, frame, pose, drop.bead, look);
    }
  }
  for (size_t n = 0; n < frame.level->nodes.size(); ++n) {
    if (frame.level->nodes[n].kind == NodeKind::Cup) DrawBerths(picture, frame, static_cast<int>(n));
  }
}

void CanalArt::DrawBarge(Canvas& canvas, const ArtFrame& frame, const barges::Pose& pose, const Bead& bead,
                         BargeLook look) const {
  const float s = frame.Scale();
  const glm::vec2 h = glm::vec2(pose.heading.x, pose.heading.y);
  const glm::vec2 w{-h.y, h.x};
  // A point `along` the barge's way and `across` it, in canvas units.
  auto at = [&](float along, float across) {
    return frame.layout.ToCanvas({pose.at.x + h.x * along + w.x * across, pose.at.y + h.y * along + w.y * across});
  };
  const float a = look.alpha * pose.fade;
  const bool murk = bead.IsMud();
  const Rgba hull = murk ? kMurkHull : Hull(*frame.palette, bead.a);
  const Rgba core = murk ? kMurkCore : Mixed(LiquidColor(*frame.palette, bead.a), Hex(0xFFFFFF), 0.6f);
  const Rgba glow = murk ? kMurkGlow : LiquidColor(*frame.palette, bead.a);
  if (look.lit) {
    // Its light: three soft glows along the hull.
    for (const float along : {-5.0f, 0.0f, 5.0f})
      glow_.Draw(canvas, at(along, 0.0f), 6.0f * s, WithAlpha(glow, (murk ? 0.5f : 0.75f) * a));
    if (pose.moving) {
      for (const float side : {-1.0f, 1.0f}) {
        canvas.Line(at(-6.0f, 2.0f * side), at(-16.0f, 5.0f * side), 1.0f * s, WithAlpha(kWake, a));
      }
    }
  }
  const float half = kBargeLength * 0.5f - kBargeWidth * 0.5f;  // the hull's round ends sit this far from its middle
  if (look.outline > 0.0f)
    canvas.Line(at(-half, 0.0f), at(half, 0.0f), (kBargeWidth + 3.0f) * s, WithAlpha(kFail, look.outline * a));
  if (bead.IsBlend()) {
    // A blend: its second colour on the back half.
    const Rgba back = murk ? kMurkHull : Hull(*frame.palette, bead.b);
    canvas.Line(at(-half, 0.0f), at(0.0f, 0.0f), kBargeWidth * s, WithAlpha(back, a));
    canvas.Line(at(0.0f, 0.0f), at(half, 0.0f), kBargeWidth * s, WithAlpha(hull, a));
  } else {
    canvas.Line(at(-half, 0.0f), at(half, 0.0f), kBargeWidth * s, WithAlpha(hull, a));
  }
  canvas.Line(at(-1.7f, 0.0f), at(1.7f, 0.0f), 2.6f * s, WithAlpha(core, a));
  if (look.lit) {
    const glm::vec2 tail = at(-kBargeLength * 0.5f - 1.0f, 0.0f);
    glow_.Draw(canvas, tail, 4.0f * s, WithAlpha(kTail, 0.45f * a * look.tail_light));
    canvas.Circle(tail, 1.6f * s, WithAlpha(kTail, a * look.tail_light));
  }
}

void CanalArt::DrawBerths(Canvas& canvas, const ArtFrame& frame, int cup) const {
  const canal::Harbour& harbour = harbours_[static_cast<size_t>(cup)];
  const canal::Frame& f = harbour.frame;
  const Vec2 mouth = f.Point(-4.0f, 0.0f);
  const int capacity = harbour.rows * canal::kBerthsPerRow;
  const Node& node = frame.level->nodes[static_cast<size_t>(cup)];
  const Marker* wrong = MomentOf(frame, Marker::WrongCup, cup);
  const float s = frame.Scale();
  int berth = 0, raft = 0, ordered = 0;
  for (const barges::Landing& landed : frame.landed[static_cast<size_t>(cup)]) {
    // A blend takes two berths (its barge sits in the first). Past the last berth, or past the order (a spill), a
    // barge rafts at the mouth.
    const float since = frame.Clock() - static_cast<float>(landed.tick);
    const float glide = barges::Glide(since);
    Vec2 to;
    if (berth < capacity && ordered < node.count) {
      to = harbour.Berth(berth);
      berth += landed.bead.Drops();
    } else {
      to = f.Point(-4.0f - 7.0f * static_cast<float>(raft / 2), raft % 2 == 0 ? 4.5f : -4.5f);
      ++raft;
    }
    ordered += landed.bead.Drops();
    const barges::Pose pose{
        {mouth.x + (to.x - mouth.x) * glide, mouth.y + (to.y - mouth.y) * glide}, {f.u.x, f.u.y}, 1.0f, glide < 1.0f};
    // A barge of the wrong colour gets a red edge in its berth while its moment plays.
    const bool pure = landed.bead.a == node.want && (!landed.bead.IsBlend() || landed.bead.b == node.want);
    const float outline = wrong && !pure && !landed.bead.IsMud() ? Progress(frame, *wrong, kFailLength) : 0.0f;
    DrawBarge(canvas, frame, pose, landed.bead, {1.0f, true, 1.0f, outline});
  }
}

void CanalArt::DrawPill(Canvas& canvas, const ArtFrame& frame, int cup) const {
  const Node& node = frame.level->nodes[static_cast<size_t>(cup)];
  const CupState& state = frame.board->Started() ? frame.board->Cup(cup) : CupState{};
  const canal::Box& box = harbours_[static_cast<size_t>(cup)].pill;
  const float s = frame.Scale();
  char count[24];
  std::snprintf(count, sizeof(count), "%d/%d", state.count, node.count);
  const bool bad = state.impure || state.spilled;
  const Rgba count_color = bad ? ui_color::kBad : state.count >= node.count ? kPillFull : kPillNamed;
  const Rgba name_color = Mixed(LiquidColor(*frame.palette, node.want), Hex(0xFFFFFF), 0.65f);
  // Glyph, name, count: at full size they may be wider than the pill, so all three shrink to fit it.
  const float pad = 6.0f, gap = 4.0f, glyph = 4.5f;
  const float wanted = (2.0f * pad + 2.0f * glyph + gap * 2.0f) * s + street_->Measure(node.district, 12.0f * s) +
                       frame.font->Measure(count, 11.0f * s);
  const float fit = std::min(1.0f, kPillWidth * s / std::max(wanted, 1.0f));
  const float width = std::min(kPillWidth * s, wanted * fit);
  const glm::vec2 centre = frame.layout.ToCanvas({(box.min.x + box.max.x) * 0.5f, (box.min.y + box.max.y) * 0.5f});
  const glm::vec2 half{width * 0.5f, kPillHeight * s * 0.5f};
  canvas.FillRect({centre - half, centre + half}, kPillBack, kPillHeight * s * 0.5f);
  const float left = centre.x - width * 0.5f + (pad + glyph) * s * fit;
  art::Glyph(canvas, node.want, {left, centre.y}, glyph * s * fit, LiquidColor(*frame.palette, node.want));
  const float name_at = left + (glyph + gap) * s * fit;
  const float name_width = street_->Measure(node.district, 12.0f * s * fit);
  canvas.Text(*street_, node.district, {name_at + name_width * 0.5f, centre.y}, 12.0f * s * fit, Align::Center,
              TextStyle::Outlined(name_color, Hex(0x080E1A)));
  const float count_at = name_at + name_width + gap * s * fit;
  const float count_width = frame.font->Measure(count, 11.0f * s * fit);
  canvas.Text(*frame.font, count, {count_at + count_width * 0.5f, centre.y}, 11.0f * s * fit, Align::Center,
              TextStyle::Outlined(count_color, Hex(0x080E1A)));
}

void CanalArt::DrawMillFront(Canvas& canvas, const ArtFrame& frame, int n) const {
  const canal::Frame& f = mills_[static_cast<size_t>(n)].frame;
  const Layout& layout = frame.layout;
  const float s = frame.Scale();
  const NodeAnim& anim = frame.anims[static_cast<size_t>(n)];
  const float clock = frame.Clock();
  const int turns = frame.board->MillTurns(n);
  // The drop in the mill (one at a time): how far through its dwell it is, so the wheel turns on through it.
  float inside = 0.0f;
  if (frame.board->Started()) {
    for (const Drop& drop : frame.board->Drops()) {
      if (!drop.alive || drop.loc.kind != Loc::InNode || drop.loc.index != n || drop.dwell_total <= 0) continue;
      inside = (clock - static_cast<float>(drop.entered)) / static_cast<float>(drop.dwell_total);
    }
  }
  const float wheel = canal::WheelAngle(turns, inside);
  for (int k = 0; k < kSlats; ++k) {
    const float theta = wheel + 2.0f * kPi * static_cast<float>(k) / static_cast<float>(kSlats);
    canvas.Line(OnWheel(layout, f, theta, 0.4f), OnWheel(layout, f, theta, 0.95f), 2.0f * s, kSlat);
  }
  // One notch lit per turn of the wheel (the sixth, then the first again).
  const int lit = turns % 6;
  const float v = -53.0f + 5.2f * static_cast<float>(lit);
  canvas.FillRect(ToCanvas(layout, f.Span(-8.0f, 0.0f, v - kNotchAcross * 0.5f, v + kNotchAcross * 0.5f)), kNotchLit,
                  1.0f * s);
  // The window glows while a barge is in the mill.
  const bool busy = frame.board->Started() && frame.board->InsideCount(n) > 0;
  glow_.Draw(canvas, layout.ToCanvas(f.Point(12.0f, -50.0f)), 6.0f * s, WithAlpha(kWindow, busy ? 0.9f : 0.25f));
  // The pulse: from the mill down each of its rods to its gate, during the tick its barge left.
  const float since = clock - static_cast<float>(anim.tick);
  const Node& node = frame.level->nodes[static_cast<size_t>(n)];
  for (size_t g = 0; g < node.gears.size(); ++g) {
    const std::optional<Vec2> at = canal::PulseAt(RodPoints(*frame.level, n, static_cast<int>(g)), since);
    if (at) glow_.Draw(canvas, layout.ToCanvas(*at), 4.0f * s, WithAlpha(kPulse, 0.9f * std::sin(kPi * since)));
  }
}

void CanalArt::DrawGateFront(Canvas& canvas, const ArtFrame& frame, int n) const {
  const canal::Gate& gate = gates_[static_cast<size_t>(n)];
  const canal::Frame& f = gate.frame;
  const Layout& layout = frame.layout;
  const float s = frame.Scale();
  const NodeAnim& anim = frame.anims[static_cast<size_t>(n)];
  // The leaves swing from the old state to the new over 0.6 tick once the flip lands (the end of its tick).
  const float open = barges::GateOpenness(frame.board->GateOpen(n), frame.Clock() - static_cast<float>(anim.flip + 1));
  for (const float side : {-1.0f, 1.0f}) {
    // Shut, the leaves meet a little upstream of the canal's middle; open, they swing round to lie along the bank.
    const float shut = std::atan2(-kGateHinge * side, -7.0f);
    const float angle = shut * (1.0f - open);
    const Vec2 tip{kGateLeaf * std::cos(angle), kGateHinge * side + kGateLeaf * std::sin(angle)};
    canvas.Line(layout.ToCanvas(f.Point(0.0f, kGateHinge * side)), layout.ToCanvas(f.Point(tip.x, tip.y)), 3.0f * s,
                kLeaf);
  }
  const glm::vec2 lamp = layout.ToCanvas(gate.lamp);
  const Rgba colour = Mixed(kLampRed, kLampGreen, open);
  glow_.Draw(canvas, lamp, 9.0f * s, WithAlpha(colour, 0.45f));
  canvas.Circle(lamp, (3.4f + 3.0f * anim.flash) * s, colour);
}

void CanalArt::DrawPayoff(Canvas& canvas, const ArtFrame& frame, int cup) const {
  if (!Served(frame, cup)) return;  // a barge more after full (or mud): the lamps stay out
  const canal::Harbour& harbour = harbours_[static_cast<size_t>(cup)];
  const canal::Frame& f = harbour.frame;
  const float s = frame.Scale();
  const float age = PaidAge(frame, cup);
  const float lamps = barges::LampsAt(age);
  glow_.Draw(canvas, frame.layout.ToCanvas(f.Point(25.0f, 0.0f)), 70.0f * s,
             WithAlpha(kWarm, 0.55f * barges::WarmAt(age)));
  for (const float along : {-16.0f, harbour.Deep() + 4.0f}) {
    for (const float across : {-40.0f, 40.0f}) {
      const glm::vec2 lamp = frame.layout.ToCanvas(f.Point(along, across));
      glow_.Draw(canvas, lamp, 6.0f * s, WithAlpha(kLampGlowLit, 0.9f * lamps));
      canvas.Circle(lamp, 1.8f * s, Mixed(kHarbourLamp, kLampLit, lamps));
    }
  }
}

void CanalArt::DrawDistrict(Canvas& canvas, const ArtFrame& frame, int cup) const {
  const canal::Box& basin = harbours_[static_cast<size_t>(cup)].basin;
  const glm::vec2 centre{(basin.min.x + basin.max.x) * 0.5f, (basin.min.y + basin.max.y) * 0.5f};
  const float reach = barges::DistrictAt(PaidAge(frame, cup));
  const float front = kDistrictNear + (kDistrictFar - kDistrictNear) * reach;
  const bool served = Served(frame, cup);
  const float s = frame.Scale();
  for (const canal::Roof& roof : city_.roofs) {
    const glm::vec2 at{(roof.box.min.x + roof.box.max.x) * 0.5f, (roof.box.min.y + roof.box.max.y) * 0.5f};
    const float d = glm::length(at - centre);
    // The ring: roofs just behind its front light up, and it dies out as it runs out. While the harbour is served,
    // the roofs nearest it stay lit.
    const float ring = (1.0f - reach) * std::clamp((front - d) / kDistrictRing, 0.0f, 1.0f);
    const float steady = served && d <= kDistrictNear ? kDistrictSteady : 0.0f;
    const float light = std::max(ring, steady);
    if (light <= 0.0f) continue;
    canvas.FillRect(ToCanvas(frame.layout, roof.box), WithAlpha(kWarm, 0.16f * light));
    for (const Vec2 window : roof.windows) {
      glow_.Draw(canvas, frame.layout.ToCanvas({window.x + 0.9f, window.y + 0.9f}), 3.0f * s,
                 WithAlpha(kSkylight, light));
    }
  }
}

bool CanalArt::DrawMarker(Canvas& canvas, const ArtFrame& frame, const Marker& marker) const {
  if (level_ != frame.level) return false;  // the canals are laid out in Prepare, before the markers are drawn
  if (marker.kind == Marker::Jam) {
    DrawJam(canvas, frame, marker);
    return true;
  }
  if (marker.node < 0) return false;
  const NodeKind kind = frame.level->nodes[static_cast<size_t>(marker.node)].kind;
  switch (marker.kind) {
    case Marker::Paid: return true;  // the harbour's art shows it (DrawFront)
    case Marker::Mud: DrawMurk(canvas, frame, marker); return true;
    case Marker::WrongCup:
    case Marker::Spill:
      if (kind != NodeKind::Cup) return false;
      Tag(canvas, frame, CupTop(frame, marker.node), marker.kind == Marker::WrongCup ? "wrong colour" : "spill",
          Progress(frame, marker, kFailLength));
      return true;
    case Marker::Dry:
      if (kind != NodeKind::Cup) return false;
      DrawShort(canvas, frame, marker);
      return true;
    default: return false;  // a near miss: the shared look
  }
}

void CanalArt::DrawMurk(Canvas& canvas, const ArtFrame& frame, const Marker& marker) const {
  const float s = frame.Scale();
  const glm::vec2 at = frame.At(marker.node);
  // The stain: along each canal that meets the junction, out from it (its reach grows to 50 board units).
  const float reach = 6.0f + 44.0f * EaseOut(Progress(frame, marker, kStainLength));
  const Node& node = frame.level->nodes[static_cast<size_t>(marker.node)];
  auto stain = [&](int pipe, bool leaving) {
    const Pipe& rules = frame.level->pipes[static_cast<size_t>(pipe)];
    const canal::Way& way = ways_[static_cast<size_t>(pipe)];
    const float length = std::min(reach, rules.length);
    std::vector<glm::vec2> line;
    for (int k = 0; k <= 25; ++k) {
      const float d = length * static_cast<float>(k) / 25.0f;
      line.push_back(frame.layout.ToCanvas(way.PointAt(leaving ? d : rules.length - d)));
    }
    const float total = Length(line);
    Trace(canvas, line, 0.0f, total, kStainWidth * s, kStain);
    for (float d = 0.0f; d < total; d += 12.0f * s) Trace(canvas, line, d, d + 6.0f * s, 2.0f * s, kStainDash);
  };
  for (const int pipe : node.ins) stain(pipe, false);
  for (const int pipe : node.outs) stain(pipe, true);
  // The cloud over the stain: soft puffs round the junction, growing out, thin enough to show the dim barges in it.
  const float r = (22.0f + 40.0f * EaseOut(Progress(frame, marker, kMurkLength))) * s;
  for (int k = 0; k < 5; ++k) {
    const float angle = 2.0f * kPi * static_cast<float>(k) / 5.0f;
    glow_.Draw(canvas, at + glm::vec2(std::cos(angle), std::sin(angle)) * (r * 0.4f), r * 0.75f,
               WithAlpha(kMurkCloud, 0.45f));
  }
  glow_.Draw(canvas, at, r * 0.8f, WithAlpha(kMurkCloudDark, 0.5f));
}

void CanalArt::DrawShort(Canvas& canvas, const ArtFrame& frame, const Marker& marker) const {
  const float s = frame.Scale();
  const int cup = marker.node;
  const canal::Harbour& harbour = harbours_[static_cast<size_t>(cup)];
  const Node& node = frame.level->nodes[static_cast<size_t>(cup)];
  const CupState state = frame.board->Started() ? frame.board->Cup(cup) : CupState{};
  const float alpha = Progress(frame, marker, kShortLength);
  const int capacity = harbour.rows * canal::kBerthsPerRow;
  const Vec2 u = harbour.frame.u, w = harbour.frame.w;
  const float half_length = kBargeLength * 0.5f, half_width = kBargeWidth * 0.5f;
  // The berths the order is still short of: dashed outlines where the barges should be.
  for (int k = state.count; k < std::min(node.count, capacity); ++k) {
    const Vec2 c = harbour.Berth(k);
    auto corner = [&](float along, float across) {
      return frame.layout.ToCanvas({c.x + u.x * along + w.x * across, c.y + u.y * along + w.y * across});
    };
    const glm::vec2 a = corner(-half_length, -half_width), b = corner(half_length, -half_width);
    const glm::vec2 d = corner(half_length, half_width), e = corner(-half_length, half_width);
    const Rgba ink = WithAlpha(kGhost, alpha);
    art::DashedLine(canvas, a, b, 1.5f * s, ink, 4);
    art::DashedLine(canvas, b, d, 1.5f * s, ink, 4);
    art::DashedLine(canvas, d, e, 1.5f * s, ink, 4);
    art::DashedLine(canvas, e, a, 1.5f * s, ink, 4);
  }
  Tag(canvas, frame, CupTop(frame, cup), "short", alpha);
}

void CanalArt::DrawJam(Canvas& canvas, const ArtFrame& frame, const Marker& marker) const {
  const float s = frame.Scale();
  const float traced = Progress(frame, marker, kJamLength);
  const JamReport jam = frame.board->Jam();
  // A red lamp beside each stuck node (cups, drains and spouts keep their own look, a gate's own lamp is already
  // red), lit as the circle is traced.
  for (const int node : jam.stuck_nodes) {
    const NodeKind kind = frame.level->nodes[static_cast<size_t>(node)].kind;
    if (kind == NodeKind::Cup || kind == NodeKind::Drain || kind == NodeKind::Spout || kind == NodeKind::Gate) continue;
    const glm::vec2 lamp = frame.layout.ToCanvas(canal::LampAt(*frame.level, node));
    glow_.Draw(canvas, lamp, 8.0f * s, WithAlpha(kFail, 0.6f * traced));
    canvas.Circle(lamp, 2.6f * s, WithAlpha(kFail, traced));
  }
  // The circle of waiting: traced round once, then it marches round.
  const std::vector<Vec2> ring = canal::JamLoop(*frame.level, jam.cycle, jam.lonely);
  if (ring.size() >= 2) {
    std::vector<glm::vec2> line;
    for (const Vec2 p : ring) line.push_back(frame.layout.ToCanvas(p));
    const float length = Length(line);
    if (traced < 1.0f) {
      Trace(canvas, line, 0.0f, length * traced, kCircleWidth * s, kFail);
    } else {
      const float period = 14.0f * s, dash = 8.0f * s, shift = std::fmod(frame.time * 20.0f * s, period);
      for (float d = -shift; d < length; d += period) Trace(canvas, line, d, d + dash, kCircleWidth * s, kFail);
    }
  }
  if (jam.lonely >= 0) {
    const bool gate = frame.level->nodes[static_cast<size_t>(jam.lonely)].kind == NodeKind::Gate;
    canvas.Text(*frame.font, gate ? "never opens" : "waits forever", frame.At(jam.lonely) + glm::vec2(0.0f, 34.0f * s),
                14.0f * s, Align::Center, TextStyle::Outlined(kFail, ui_color::kBackground, traced));
  }
}

float CanalArt::MomentLength(const Marker& marker) const {
  switch (marker.kind) {
    case Marker::Paid: return barges::kDistrictLength;
    case Marker::Mud: return kStainLength;
    case Marker::WrongCup:
    case Marker::Spill: return kFailLength;
    case Marker::Dry: return kShortLength;
    case Marker::Jam: return kJamLength;
    default: return 0.0f;
  }
}

void CanalArt::DrawFront(Canvas& canvas, const ArtFrame& frame) {
  Prepare(frame);
  const Level& level = *frame.level;
  const Layout& layout = frame.layout;
  const float s = frame.Scale();
  for (size_t n = 0; n < level.nodes.size(); ++n) {
    if (level.nodes[n].kind != NodeKind::Cup) continue;
    DrawDistrict(canvas, frame, static_cast<int>(n));
    DrawPayoff(canvas, frame, static_cast<int>(n));
  }
  for (size_t n = 0; n < level.nodes.size(); ++n) {
    const Node& node = level.nodes[n];
    switch (node.kind) {
      case NodeKind::Spout: {
        // The lock's two brass gates: shut meeting a little ahead of the chamber's middle; open (swung back
        // against the chamber's sides) while the lock pours.
        const canal::Frame& f = locks_[n].frame;
        const float open = barges::LockOpen(frame.Clock() - static_cast<float>(frame.anims[n].tick));
        for (const float side : {-1.0f, 1.0f}) {
          const Vec2 shut{19.0f, 1.0f * side}, swung{5.0f, 12.0f * side};
          const Vec2 tip{shut.x + (swung.x - shut.x) * open, shut.y + (swung.y - shut.y) * open};
          canvas.Line(layout.ToCanvas(f.Point(16.0f, 12.0f * side)), layout.ToCanvas(f.Point(tip.x, tip.y)), 2.6f * s,
                      kLeaf);
        }
        break;
      }
      case NodeKind::Cup: DrawPill(canvas, frame, static_cast<int>(n)); break;
      case NodeKind::Mill: DrawMillFront(canvas, frame, static_cast<int>(n)); break;
      case NodeKind::Gate: DrawGateFront(canvas, frame, static_cast<int>(n)); break;
      default: break;
    }
  }
}

wlw::ui::Rect CanalArt::CupPool(const ArtFrame& frame, int cup) const {
  const canal::Harbour& harbour = harbours_[static_cast<size_t>(cup)];
  return ToCanvas(frame.layout, harbour.frame.Span(18.0f, harbour.Deep() - 27.0f, -40.0f, 40.0f));
}

glm::vec2 CanalArt::CupTop(const ArtFrame& frame, int cup) const {
  const canal::Box& basin = harbours_[static_cast<size_t>(cup)].basin;
  return frame.layout.ToCanvas({(basin.min.x + basin.max.x) * 0.5f, basin.min.y - 8.0f});
}

void CanalArt::DrawMud(Canvas& canvas, glm::vec2 at, float age, float scale, float time) const {
  parts_.DrawMud(canvas, at, age, scale, time);
}

glm::vec2 CanalArt::PipePoint(const ArtFrame& frame, int pipe, float distance) const {
  const Pipe& rules = frame.level->pipes[static_cast<size_t>(pipe)];
  if (level_ != frame.level || static_cast<size_t>(pipe) >= ways_.size() || rules.length <= 0.0f)
    return BoardArt::PipePoint(frame, pipe, distance);
  return frame.layout.ToCanvas(ways_[static_cast<size_t>(pipe)].PointAt(distance));
}

float CanalArt::SocketRadius(const Node& node) const {
  return node.kind == NodeKind::Cross || node.kind == NodeKind::Merge ? 15.0f : 13.0f;
}

}  // namespace threadlock
