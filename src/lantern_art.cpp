#include "lantern_art.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "liquid.h"
#include "workshop_art.h"
#include "wlw/rendering/shader.h"

namespace threadlock {
namespace {

using art::kPi;
using wlw::ui::Align;
using wlw::ui::Canvas;
using wlw::ui::Rect;
using wlw::ui::TextStyle;

// The glassworks' colours.
constexpr Rgba kGlass = Hex(0xE4F0FF);
constexpr Rgba kGlassRim = Hex(0x93A3B8);    // a tube's rim as it reads over the dark wall
constexpr Rgba kGlassShine = Hex(0x76839A);  // the reflection along a tube
constexpr Rgba kGlassBody = Hex(0xDCEBFF, 0.075f);
constexpr Rgba kChain = Hex(0xB98B47);
constexpr Rgba kBar = Hex(0xB88A48);  // a lantern's bars
constexpr Rgba kSmoke = Hex(0x767B84);
constexpr Rgba kPillInk = Hex(0x080E18);

// Sizes, in board units.
constexpr float kTubeRadius = 9.0f;  // outer
constexpr float kRim = 2.3f;         // the glass's thickness at the rim
constexpr float kCoilTube = 7.0f;    // a coil's glass is thinner
constexpr float kBend = 24.0f;       // how round a tube's bends are
constexpr float kGlobe = 24.0f;      // a crossing's or a merge's globe
constexpr float kHub = 14.0f;        // a fork's glass hub
constexpr float kNozzleGap = 20.0f;  // drops fall this far from a tube's end into a lantern

// A lantern hangs under its cup node (`at` = its mouth, where drops land). Offsets from `at`.
constexpr float kCapBottom = 10.0f, kGlassBottom = 56.0f, kFootBottom = 64.0f;
constexpr float kLanternHalf = 21.0f, kCapTopHalf = 11.0f, kCapHalf = 17.0f, kChamfer = 4.0f;
constexpr float kPillY = 82.0f;

// The crucible over a spout node (`at`): its rim and bowl.
constexpr float kRimTop = -16.0f, kRimBottom = -10.0f, kBowlBottom = 15.0f, kSurface = -12.0f;

glm::vec2 Unit(glm::vec2 v) {
  const float length = glm::length(v);
  return length > 1e-5f ? v / length : glm::vec2(0.0f, 1.0f);
}

glm::vec2 Left(glm::vec2 d) { return {d.y, -d.x}; }

glm::vec2 V(Vec2 p) { return {p.x, p.y}; }

uint64_t Mix(uint64_t h, uint64_t v) {
  h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
  return h;
}

uint64_t Bits(float f) {
  uint32_t u = 0;
  std::memcpy(&u, &f, sizeof(u));
  return u;
}

float Hash01(uint32_t x) {
  x ^= x >> 16;
  x *= 0x7FEB352Du;
  x ^= x >> 15;
  x *= 0x846CA68Bu;
  x ^= x >> 16;
  return static_cast<float>(x & 0xFFFF) / 65535.0f;
}

// How far from a node's centre a tube's glass stops, for the part drawn on that node.
float GlassStop(const Node& node) {
  switch (node.kind) {
    case NodeKind::Spout: return node.radius;  // the tube starts under the crucible's nozzle
    case NodeKind::Fork: return kHub;
    case NodeKind::Cross:
    case NodeKind::Merge: return kGlobe;
    case NodeKind::Mill: return 16.0f;
    case NodeKind::Drain: return 13.0f;
    case NodeKind::Cup:
    case NodeKind::Rack: return kNozzleGap;
    case NodeKind::Pass:
    case NodeKind::Gate: return 0.0f;
  }
  return 0.0f;
}

bool Collared(const Node& node) {
  return node.kind == NodeKind::Spout || node.kind == NodeKind::Cross || node.kind == NodeKind::Merge ||
         node.kind == NodeKind::Cup || node.kind == NodeKind::Rack || node.kind == NodeKind::Drain;
}

// The lantern's glass, an octagon (canvas units).
std::vector<glm::vec2> LanternGlass(glm::vec2 top, float s) {
  const float h = kLanternHalf, c = kChamfer, y0 = kCapBottom, y1 = kGlassBottom;
  return {top + glm::vec2(-h + c, y0) * s, top + glm::vec2(h - c, y0) * s, top + glm::vec2(h, y0 + c) * s,
          top + glm::vec2(h, y1 - c) * s,  top + glm::vec2(h - c, y1) * s, top + glm::vec2(-h + c, y1) * s,
          top + glm::vec2(-h, y1 - c) * s, top + glm::vec2(-h, y0 + c) * s};
}

void Closed(Canvas& canvas, const std::vector<glm::vec2>& points, float thickness, const Rgba& color) {
  for (size_t i = 0; i < points.size(); ++i) canvas.Line(points[i], points[(i + 1) % points.size()], thickness, color);
}

}  // namespace

void LanternArt::Create(wlw::app::Application& app) {
  using namespace wlw;
  wall_shader_ = rendering::Shader::FromFile("shaders/lantern_wall.shader");
  pane_texture_ =
      rendering::WTexture2D::Create(kPanes, 1, {rendering::TextureFormat::RGBA8, false, false}, pane_pixels_.data());
  tiles_.Create(app.Window(), "Lantern Works", Hex(0x080C15));
  AddTile();  // every board needs one: made up front, so the shader compiles in the warm-up
}

void LanternArt::AddTile() {
  tiles_.Add();
  TileArt art;
  art.wall = wlw::rendering::ShaderMaterial::Create(wall_shader_);
  art.wall->SetParam("uPanes", pane_texture_);
  tile_art_.push_back(std::move(art));
}

void LanternArt::Invalidate() { tiles_.Invalidate(); }

void LanternArt::Release() {
  // Only a tall board has more than one tile.
  tiles_.Shrink(1);
  tile_art_.resize(1);
  for (TileArt& art : tile_art_) art.tubes.clear();
  Invalidate();
}

// ---- Geometry ----

void LanternArt::Prepare(const ArtFrame& frame) {
  const Layout layout = frame.layout.Unscrolled();
  if (level_ == frame.level && layout_ == layout && tubes_.size() == frame.level->pipes.size()) return;
  level_ = frame.level;
  layout_ = layout;
  tubes_ = MakeTubes(*frame.level, layout_);
}

std::vector<LanternArt::Tube> LanternArt::MakeTubes(const Level& level, const Layout& layout) const {
  const float s = layout.scale;
  std::vector<Tube> tubes;
  for (const Pipe& pipe : level.pipes) {
    Tube tube;
    std::vector<glm::vec2> points;
    for (Vec2 p : pipe.points) points.push_back(layout.ToCanvas(p));
    points = art::RoundCorners(points, kBend * s, 2.5f * s);
    tube.centre = art::MakePath(points);
    tube.stretch = pipe.length > 0.0f ? tube.centre.Length() / pipe.length : 1.0f;
    const Node& from = level.nodes[static_cast<size_t>(pipe.from)];
    const Node& to = level.nodes[static_cast<size_t>(pipe.to)];
    std::vector<glm::vec2> whole;
    const glm::vec2 from_at = layout.ToCanvas(from.at), to_at = layout.ToCanvas(to.at);
    if (glm::distance(from_at, points.front()) > 0.01f) whole.push_back(from_at);
    whole.insert(whole.end(), points.begin(), points.end());
    if (glm::distance(to_at, points.back()) > 0.01f) whole.push_back(to_at);
    tube.whole = art::MakePath(std::move(whole));
    tube.lead = glm::distance(from_at, points.front());
    auto on_whole = [&](float rules_distance) { return tube.lead + rules_distance * tube.stretch; };
    // Glass from part to part, with gaps for the coil and the drip fall.
    std::vector<glm::vec2> gaps;
    if (pipe.coil_end > pipe.coil_start) {
      tube.coil_at = {on_whole(pipe.coil_start), on_whole(pipe.coil_end)};
      gaps.push_back(tube.coil_at);
      for (Vec2 p : CoilPoints(pipe.coil_a, pipe.coil_b, pipe.coil_turns)) tube.coil.push_back(layout.ToCanvas(p));
    }
    if (pipe.drip_end > pipe.drip_start) {
      tube.drip = {on_whole(pipe.drip_start), on_whole(pipe.drip_end)};
      gaps.push_back(tube.drip);
    }
    std::sort(gaps.begin(), gaps.end(), [](glm::vec2 a, glm::vec2 b) { return a.x < b.x; });
    float at = GlassStop(from) * s;
    const float end = tube.whole.Length() - GlassStop(to) * s;
    for (const glm::vec2 gap : gaps) {
      if (gap.x > at + 1.0f) tube.glass.push_back({at, std::min(gap.x, end)});
      at = std::max(at, gap.y);
    }
    if (end > at + 1.0f) tube.glass.push_back({at, end});
    tube.collar_start = Collared(from);
    tube.collar_end = Collared(to);
    tubes.push_back(std::move(tube));
  }
  return tubes;
}

glm::vec2 LanternArt::PipePoint(const ArtFrame& frame, int pipe, float distance) const {
  const Pipe& rules = frame.level->pipes[static_cast<size_t>(pipe)];
  if (level_ != frame.level || static_cast<size_t>(pipe) >= tubes_.size() || rules.length <= 0.0f)
    return BoardArt::PipePoint(frame, pipe, distance);
  const Tube& tube = tubes_[static_cast<size_t>(pipe)];
  float at = tube.lead + distance * tube.stretch;
  // Falling: slow off the nozzle, then faster (the drop keeps its ticks; only its look accelerates).
  if (tube.drip.y > tube.drip.x && at > tube.drip.x && at < tube.drip.y) {
    const float t = (at - tube.drip.x) / (tube.drip.y - tube.drip.x);
    at = tube.drip.x + (tube.drip.y - tube.drip.x) * t * t;
  }
  return tube.whole.PointAt(at) + frame.layout.ScrollOffset();
}

float LanternArt::WindowRadius(const ArtFrame& frame) const { return 172.0f * frame.Scale(); }

float LanternArt::SocketRadius(const Node& node) const {
  return node.kind == NodeKind::Cross || node.kind == NodeKind::Merge ? kGlobe + 6.0f : 26.0f;
}

// ---- The window's light ----

void LanternArt::UpdatePanes(const ArtFrame& frame) {
  const Level& level = *frame.level;
  const Board& board = *frame.board;
  std::array<float, kPanes> target{};
  // The outer panes are shared out among the lanterns in order; each lights its panes one by one as
  // it fills, in its own colour (a lantern with mud in it stays dark).
  for (size_t n = 0; n < level.nodes.size(); ++n) {
    const Node& node = level.nodes[n];
    if (node.kind != NodeKind::Cup) continue;
    const PaneRange range = LanternPanes(level, static_cast<int>(n));
    const int first = range.first, last = range.last, panes = range.Count();
    const CupState& cup = board.Started() ? board.Cup(static_cast<int>(n)) : CupState{};
    const float filled = cup.impure || node.count <= 0
                             ? 0.0f
                             : std::min(1.0f, static_cast<float>(cup.count) / static_cast<float>(node.count));
    for (int p = first; p < last; ++p) {
      target[static_cast<size_t>(p)] =
          std::clamp(filled * static_cast<float>(panes) - static_cast<float>(p - first), 0.0f, 1.0f);
      pane_color_[static_cast<size_t>(p)] = Mixed(LiquidColor(*frame.palette, node.want), Hex(0xFFFFFF), 0.12f);
    }
  }
  // A solved machine lights the window's heart.
  const bool solved = board.Started() && !board.Running() && board.GetOutcome().solved;
  for (int p = kOuterPanes; p < kPanes; ++p) {
    target[static_cast<size_t>(p)] = solved ? 1.0f : 0.0f;
    pane_color_[static_cast<size_t>(p)] = p % 2 == 0 ? Hex(0x8FB8FF) : Hex(0xFFD27A);
  }
  const float rate = frame.calm ? 1.0f : std::min(1.0f, frame.dt * 3.5f);
  for (size_t p = 0; p < pane_lit_.size(); ++p) {
    // The heart lights pane by pane, round the circle.
    float want = target[p];
    if (p >= kOuterPanes && solved && !frame.calm) {
      const float order = static_cast<float>(p - kOuterPanes) / static_cast<float>(kInnerPanes);
      if (pane_lit_[kOuterPanes] < order * 0.98f && p > kOuterPanes) want = pane_lit_[p];
    }
    pane_lit_[p] += (want - pane_lit_[p]) * rate;
    if (std::fabs(pane_lit_[p] - want) < 0.004f) pane_lit_[p] = want;
  }
}

// ---- Drawing ----

void LanternArt::DrawBack(Canvas& picture, const ArtFrame& frame) {
  Prepare(frame);
  UpdatePanes(frame);
  // The still picture, in screen-sized tiles: each redrawn only when something in it changed.
  const glm::vec2 screen = picture.Size();
  const float h = screen.y;
  const float scroll = frame.layout.scroll * frame.layout.scale;  // canvas units
  const float total = h + frame.layout.scroll_max * frame.layout.scale;
  const TilePool::Span span = TilePool::Visible(scroll, h, total);
  const int first = span.first, last = span.last;
  uint64_t board_key = Mix(0, reinterpret_cast<uintptr_t>(frame.level));
  board_key = Mix(board_key, Bits(layout_.origin.x));
  board_key = Mix(board_key, Bits(layout_.origin.y));
  board_key = Mix(board_key, Bits(layout_.scale));
  board_key = Mix(board_key, Bits(layout_.scroll_max));
  board_key = Mix(board_key, Bits(screen.x));
  board_key = Mix(board_key, Bits(screen.y));
  board_key = Mix(board_key, Bits(picture.PixelsPerUnit()));
  board_key = Mix(board_key, reinterpret_cast<uintptr_t>(frame.palette));
  uint64_t light_key = 0;
  for (size_t p = 0; p < pane_lit_.size(); ++p) {
    light_key = Mix(light_key, static_cast<uint64_t>(std::lround(pane_lit_[p] * 48.0f)));
    light_key = Mix(light_key, wlw::rendering::PackColor(pane_color_[p]));
  }
  // The window's light reaches only the tiles round the window and its beam (still coordinates).
  const float window_y = layout_.ToCanvas(frame.level->look.window).y, radius = WindowRadius(frame);
  const float lit_top = window_y - 1.05f * radius, lit_bottom = window_y + 0.3f * radius + 0.9f * h;
  // The slots this board needs (a grand machine is two screens: one each), made up front: a new one
  // waits a frame before it can draw.
  while (tiles_.Count() < std::min(span.count, 3)) AddTile();
  struct Want {
    uint64_t base, key;
  };
  auto want = [&](int k) {
    const uint64_t base = Mix(board_key, static_cast<uint64_t>(k) + 1u);
    const float top = static_cast<float>(k) * h;
    const bool lit = top < lit_bottom && top + h > lit_top;
    return Want{base, lit ? Mix(base, light_key) : base};
  };
  // A tile showing another board (or nothing) draws now; of those that only need the new light, one a
  // frame (the most visible first), which keeps the cost at one still picture's.
  int stale = -1, stale_k = -1;
  float stale_shown = 0.0f;
  for (int k = first; k <= last; ++k) {
    const int slot = tiles_.Hold(k, first, last);
    if (slot < 0 || tiles_[slot].fresh) continue;
    TilePool::Slot& tile = tiles_[slot];
    const Want w = want(k);
    if (tile.base != w.base) {
      DrawStill(slot, k, frame, screen, total);
      tile.base = w.base;
      tile.key = w.key;
    } else if (tile.key != w.key) {
      const float top = static_cast<float>(k) * h;
      const float shown = std::min(top + h, scroll + h) - std::max(top, scroll);
      if (shown > stale_shown) stale = slot, stale_k = k, stale_shown = shown;
    }
  }
  if (stale >= 0) {
    DrawStill(stale, stale_k, frame, screen, total);
    tiles_[stale].key = want(stale_k).key;
  }
  tiles_.Draw(picture, first, last, scroll);

  const Level& level = *frame.level;
  const Board& board = *frame.board;
  const float s = frame.Scale();
  tiles_.EndFrame();

  // Lanterns glow as they fill.
  for (size_t n = 0; n < level.nodes.size(); ++n) {
    const Node& node = level.nodes[n];
    if (node.kind != NodeKind::Cup) continue;
    const CupState& cup = board.Started() ? board.Cup(static_cast<int>(n)) : CupState{};
    const float fill = std::clamp(frame.cup_fill[n] / static_cast<float>(std::max(1, node.count)), 0.0f, 1.0f);
    if (fill <= 0.01f || cup.impure) continue;
    const glm::vec2 c = frame.At(static_cast<int>(n)) + glm::vec2(0.0f, (kCapBottom + kGlassBottom) * 0.5f * s);
    const float flicker = frame.calm ? 1.0f : 0.92f + 0.08f * std::sin(frame.time * 7.0f + static_cast<float>(n));
    glow_.Draw(picture, c, (34.0f + 34.0f * fill) * s,
               WithAlpha(LiquidColor(*frame.palette, node.want), 0.42f * fill * flicker));
  }
  // Dust in the light.
  if (!frame.calm) {
    const glm::vec2 size = picture.Size();
    for (uint32_t i = 0; i < 16; ++i) {
      const float x = Hash01(i * 7919u + 1u) * size.x + std::sin(frame.time * 0.3f + static_cast<float>(i)) * 6.0f;
      const float y = std::fmod(Hash01(i * 104729u + 3u) * size.y - frame.time * (4.0f + 3.0f * Hash01(i)), size.y);
      const float a = 0.25f + 0.25f * Hash01(i * 31u + 7u);
      picture.Circle({x, y < 0.0f ? y + size.y : y}, 0.6f + 0.6f * Hash01(i * 13u), WithAlpha(kGlass, a));
    }
  }
}

void LanternArt::DrawStill(int slot, int k, const ArtFrame& whole, glm::vec2 screen, float total) {
  TilePool::Slot& tile = tiles_[slot];
  TileArt& art = tile_art_[static_cast<size_t>(slot)];
  // A tile made this frame has no size yet (its canvas gets one when it is first drawn): it is the screen's.
  Canvas& canvas = tile.view->GetCanvas();
  // This tile's piece of the board: the layout at scroll 0, moved up by the tiles above it.
  ArtFrame frame = whole;
  frame.layout = layout_;
  frame.layout.origin.y -= static_cast<float>(k) * screen.y;
  if (k > 0 && (!(art.layout == frame.layout) || art.tubes.size() != frame.level->pipes.size())) {
    art.layout = frame.layout;
    art.tubes = MakeTubes(*frame.level, frame.layout);
  }
  const std::vector<Tube>& tubes = k == 0 ? tubes_ : art.tubes;
  tile.view->SetUpdateMode(wlw::scene::Viewport::UpdateMode::Once);

  // The wall and the window (one shader), with the window's light.
  for (size_t p = 0; p < pane_lit_.size(); ++p) {
    const Rgba& c = pane_color_[p];
    pane_pixels_[p * 4 + 0] = static_cast<uint8_t>(std::lround(std::clamp(c.r, 0.0f, 1.0f) * 255.0f));
    pane_pixels_[p * 4 + 1] = static_cast<uint8_t>(std::lround(std::clamp(c.g, 0.0f, 1.0f) * 255.0f));
    pane_pixels_[p * 4 + 2] = static_cast<uint8_t>(std::lround(std::clamp(c.b, 0.0f, 1.0f) * 255.0f));
    pane_pixels_[p * 4 + 3] = static_cast<uint8_t>(std::lround(std::clamp(pane_lit_[p], 0.0f, 1.0f) * 255.0f));
  }
  pane_texture_->Update(pane_pixels_.data());
  glm::vec3 beam{0.0f};
  float lit = 0.0f;
  for (int p = 0; p < kOuterPanes; ++p) {
    const Rgba& c = pane_color_[static_cast<size_t>(p)];
    beam += glm::vec3(c.r, c.g, c.b) * pane_lit_[static_cast<size_t>(p)];
    lit += pane_lit_[static_cast<size_t>(p)];
  }
  if (lit > 0.0f) beam /= lit;
  // The shader works in still coordinates: this tile starts uOffset down the whole picture.
  const glm::vec2 centre = layout_.ToCanvas(frame.level->look.window);
  art.wall->SetParam("uCanvas", screen);
  art.wall->SetParam("uOffset", glm::vec2(0.0f, static_cast<float>(k) * screen.y));
  art.wall->SetParam("uTotal", glm::vec2(screen.x, total));
  art.wall->SetParam("uScreen", screen);
  art.wall->SetParam("uWindow", glm::vec3(centre, WindowRadius(frame)));
  art.wall->SetParam("uBeam", glm::vec4(beam, lit / static_cast<float>(kOuterPanes)));
  canvas.ShaderRect({{0.0f, 0.0f}, screen}, art.wall);

  const Level& level = *frame.level;
  // Behind the glass: crucibles, the lanterns' chains, the globes' and hubs' backs.
  for (size_t n = 0; n < level.nodes.size(); ++n) {
    const Node& node = level.nodes[n];
    if (node.kind == NodeKind::Spout) DrawCrucible(canvas, frame, static_cast<int>(n), false);
    if (node.kind == NodeKind::Cup) DrawLantern(canvas, frame, static_cast<int>(n), false);
  }
  for (size_t p = 0; p < level.pipes.size(); ++p) DrawTube(canvas, frame, tubes[p], static_cast<int>(p));
  const float s = frame.Scale();
  for (size_t n = 0; n < level.nodes.size(); ++n) {
    const Node& node = level.nodes[n];
    const glm::vec2 c = frame.At(static_cast<int>(n));
    if (node.kind == NodeKind::Cross || node.kind == NodeKind::Merge)
      DrawGlobe(canvas, frame, static_cast<int>(n), false);
    if (node.kind == NodeKind::Fork) {
      canvas.Circle(c, kHub * s, Hex(0x7FA8D8, 0.08f));
      canvas.Ring(c, (kHub - 1.0f) * s, 2.0f * s, kGlassRim);
    }
  }
}

void LanternArt::DrawGlass(Canvas& canvas, const art::Path& path, float radius, bool in_front, float s) const {
  if (path.Empty()) return;
  // A soft shadow on the wall behind it.
  if (in_front) {
    art::Path shadow = path;
    for (glm::vec2& point : shadow.points) point += glm::vec2(1.5f, 6.0f) * s;
    art::Ribbon(canvas, shadow, -(radius + 1.5f) * s, (radius + 1.5f) * s, Rgba{0.0f, 0.0f, 0.0f, 0.16f});
    art::Ribbon(canvas, shadow, -(radius - 1.0f) * s, (radius - 1.0f) * s, Rgba{0.0f, 0.0f, 0.0f, 0.14f});
  }
  // Clear glass: the wall seen through it, a little brighter, and its rims (dimmer on a coil's far side).
  const Rgba rim = in_front ? kGlassRim : Mixed(kGlassRim, Hex(0x101826), 0.55f);
  art::Ribbon(canvas, path, -(radius - kRim) * s, (radius - kRim) * s, in_front ? kGlassBody : Hex(0xDCEBFF, 0.04f));
  art::Stroke(canvas, path, -(radius - kRim * 0.5f) * s, kRim * s, rim);
  art::Stroke(canvas, path, (radius - kRim * 0.5f) * s, kRim * s, rim);
  // The reflection: a thin line along its upper left side.
  if (in_front && path.Length() > 8.0f * s) {
    art::Path shine = path;
    for (glm::vec2& point : shine.points) point += glm::vec2(-2.8f, -2.4f) * s * (radius / kTubeRadius);
    art::Stroke(canvas, shine.Part(3.0f * s, shine.Length() - 3.0f * s), 0.0f, 1.3f * s, kGlassShine);
  }
}

void LanternArt::DrawCoil(Canvas& canvas, const Tube& tube, float s) const {
  // Turn by turn: first every stretch on the far side, then the in_front ones over them.
  const int stretches = static_cast<int>(tube.coil.size()) - 1;
  for (int pass = 0; pass < 2; ++pass) {
    const bool in_front = pass == 1;
    int k = 0;
    while (k < stretches) {
      if (CoilFront(k) != in_front) {
        ++k;
        continue;
      }
      std::vector<glm::vec2> run{tube.coil[static_cast<size_t>(k)]};
      while (k < stretches && CoilFront(k) == in_front) run.push_back(tube.coil[static_cast<size_t>(++k)]);
      DrawGlass(canvas, art::MakePath(art::RoundCorners(run, 6.0f * s, 1.5f * s)), kCoilTube, in_front, s);
    }
  }
}

void LanternArt::DrawTube(Canvas& canvas, const ArtFrame& frame, const Tube& tube, int p) const {
  const float s = frame.Scale();
  for (const glm::vec2 run : tube.glass) DrawGlass(canvas, tube.whole.Part(run.x, run.y), kTubeRadius, true, s);
  if (!tube.coil.empty()) DrawCoil(canvas, tube, s);
  // Etched graduations, one per cell (every fifth longer): the timing, like a measuring tube's.
  const Pipe& pipe = frame.level->pipes[static_cast<size_t>(p)];
  for (int k = 1; k < pipe.cells; ++k) {
    const float d = tube.lead + tube.centre.Length() * static_cast<float>(k) / static_cast<float>(pipe.cells);
    bool on_glass = false;
    for (const glm::vec2 run : tube.glass) on_glass = on_glass || (d > run.x + 3.0f * s && d < run.y - 3.0f * s);
    if (!on_glass) continue;  // under a globe, in a drip or a coil
    const glm::vec2 at = tube.whole.PointAt(d);
    const glm::vec2 right = -Left(tube.whole.DirectionAt(d));
    const float inner = (k % 5 == 0 ? kTubeRadius - kRim - 4.5f : kTubeRadius - kRim - 2.4f) * s;
    canvas.Line(at + right * inner, at + right * ((kTubeRadius - kRim - 0.4f) * s), 0.9f * s, Hex(0xC4D4EA, 0.3f));
  }
}

void LanternArt::DrawGlobe(Canvas& canvas, const ArtFrame& frame, int n, bool front) const {
  const float s = frame.Scale();
  const glm::vec2 c = frame.At(n);
  const float r = kGlobe * s;
  if (!front) {
    canvas.Circle(c, r, Hex(0x7FA8D8, 0.07f));
    canvas.Circle(c - glm::vec2(5.0f, 6.0f) * s, 14.0f * s, Hex(0xF0F8FF, 0.06f));
    canvas.Ring(c, r - 2.5f * s, 4.0f * s, Hex(0xDCEBFF, 0.1f));
    // The far half of its brass band, behind the liquid.
    art::EllipseArc(canvas, c, glm::vec2(kGlobe, 6.0f) * s, 2.6f * s, art::brass::kShade, kPi, 2.0f * kPi);
    return;
  }
  canvas.Ring(c, r, 2.0f * s, WithAlpha(kGlass, 0.62f));
  art::EllipseArc(canvas, c, glm::vec2(kGlobe, 6.0f) * s, 3.0f * s, art::brass::kMid, 0.0f, kPi);
  art::EllipseArc(canvas, c + glm::vec2(0.0f, -0.8f) * s, glm::vec2(kGlobe - 1.0f, 5.2f) * s, 1.0f * s,
                  art::brass::kLight, 0.15f * kPi, 0.85f * kPi);
  canvas.Arc(c, r - 4.0f * s, 2.4f * s, -0.42f * kPi, 0.3f * kPi, Hex(0xFFFFFF, 0.5f));
  // Brass collars where the tubes meet it.
  const Level& level = *frame.level;
  const Node& node = level.nodes[static_cast<size_t>(n)];
  auto collar = [&](glm::vec2 dir) {
    art::BrassBar(canvas, c + dir * (r + 0.5f * s), glm::vec2(9.5f, 2.6f) * s, std::atan2(dir.y, dir.x) + kPi * 0.5f);
  };
  for (int pipe : node.outs) collar(Unit(V(level.pipes[static_cast<size_t>(pipe)].points.front()) - V(node.at)));
  for (int pipe : node.ins) collar(Unit(V(level.pipes[static_cast<size_t>(pipe)].points.back()) - V(node.at)));
}

void LanternArt::DrawCrucible(Canvas& canvas, const ArtFrame& frame, int n, bool front) const {
  const float s = frame.Scale();
  const glm::vec2 c = frame.At(n);
  const Node& node = frame.level->nodes[static_cast<size_t>(n)];
  auto at = [&](float x, float y) { return c + glm::vec2(x, y) * s; };
  if (!front) {
    // It hangs on three chains.
    const glm::vec2 apex = at(0.0f, kRimTop - 40.0f);
    for (float x : {-28.0f, 0.0f, 28.0f}) art::DashedLine(canvas, at(x, kRimTop + 1.0f), apex, 1.5f * s, kChain, 9);
    canvas.Circle(apex, 2.4f * s, art::brass::kShade);
    // The bowl, lit from the left.
    const std::vector<glm::vec2> bowl{at(-29.0f, kRimBottom - 1.0f),
                                      at(29.0f, kRimBottom - 1.0f),
                                      at(25.0f, -2.0f),
                                      at(20.0f, 6.0f),
                                      at(12.0f, 12.0f),
                                      at(0.0f, kBowlBottom),
                                      at(-12.0f, 12.0f),
                                      at(-20.0f, 6.0f),
                                      at(-25.0f, -2.0f)};
    art::FillConvex(canvas, bowl, art::brass::kMid);
    art::FillConvex(canvas,
                    {at(6.0f, kRimBottom - 1.0f), at(29.0f, kRimBottom - 1.0f), at(25.0f, -2.0f), at(20.0f, 6.0f),
                     at(12.0f, 12.0f), at(2.0f, kBowlBottom - 0.3f)},
                    art::brass::kShade);
    canvas.Line(at(-21.0f, -6.0f), at(-13.0f, 7.0f), 2.6f * s, WithAlpha(art::brass::kLight, 0.7f));
    canvas.Line(at(18.0f, 4.0f), at(9.0f, 11.0f), 2.0f * s, WithAlpha(art::brass::kDark, 0.8f));
    // Its nozzle down to the tube.
    canvas.Line(at(0.0f, kBowlBottom - 2.0f), at(0.0f, node.radius - 2.0f), 6.0f * s, art::brass::kShade);
    canvas.Line(at(-1.0f, kBowlBottom - 2.0f), at(-1.0f, node.radius - 2.0f), 2.0f * s, art::brass::kMid);
    // The rim.
    canvas.FillRect({at(-33.0f, kRimTop), at(33.0f, kRimBottom)}, art::brass::kShade, 2.5f * s);
    canvas.FillRect({at(-33.0f, kRimTop), at(32.0f, kRimBottom - 1.2f)}, art::brass::kMid, 2.5f * s);
    canvas.FillRect({at(-30.0f, kRimTop + 0.8f), at(20.0f, kRimTop + 2.2f)}, WithAlpha(art::brass::kLight, 0.8f),
                    0.7f * s);
    // A dark mouth where the melt sits.
    canvas.FillRect({at(-27.0f, kSurface - 3.6f), at(27.0f, kSurface + 3.6f)}, Hex(0x2A1A08), 3.6f * s);
    return;
  }
  // In front: the drops still to pour, in order, in a row over the rim (blends show both colours).
  const int poured = frame.board->Started() ? frame.board->Poured(n) : 0;
  const int left = static_cast<int>(node.beads.size()) - poured;
  constexpr int kPerRow = 8;
  for (int k = 0; k < left; ++k) {
    const int row = k / kPerRow, col = k % kPerRow;
    const int in_row = std::min(kPerRow, left - row * kPerRow);
    const glm::vec2 p =
        at((static_cast<float>(col) - static_cast<float>(in_row - 1) * 0.5f) * 9.0f, kRimTop - 9.0f - 9.0f * row);
    canvas.Circle(p, 4.6f * s, Hex(0x05080C, 0.7f));
    DrawBead(canvas, node.beads[static_cast<size_t>(poured + k)], p, 3.5f * s, *frame.palette, 1.0f);
  }
}

void LanternArt::DrawLantern(Canvas& canvas, const ArtFrame& frame, int n, bool front) const {
  const float s = frame.Scale();
  const glm::vec2 top = frame.At(n);
  auto at = [&](float x, float y) { return top + glm::vec2(x, y) * s; };
  const std::vector<glm::vec2> glass = LanternGlass(top, s);
  if (!front) {
    // Chains from the tube's nozzle to the cap, and the glass's back.
    for (float x : {-1.0f, 1.0f})
      art::DashedLine(canvas, at(5.0f * x, -kNozzleGap + 2.0f), at(10.0f * x, 0.5f), 1.4f * s, kChain, 4);
    art::FillConvex(canvas, glass, Hex(0xDCEBFF, 0.05f));
    art::FillConvex(canvas, LanternGlass(top + glm::vec2(0.0f, 3.0f) * s, s * 0.86f), Hex(0x060A12, 0.3f));
    return;
  }
  const Node& node = frame.level->nodes[static_cast<size_t>(n)];
  const Board& board = *frame.board;
  const CupState& cup = board.Started() ? board.Cup(n) : CupState{};
  // The glass's rim, its bars and a reflection.
  Closed(canvas, glass, 1.7f * s, Hex(0xB9C8DC));
  for (float x : {-7.5f, 7.5f}) canvas.Line(at(x, kCapBottom), at(x, kGlassBottom), 1.5f * s, kBar);
  canvas.Line(at(-15.0f, kCapBottom + 7.0f), at(-15.0f, kGlassBottom - 7.0f), 1.8f * s, Hex(0xFFFFFF, 0.28f));
  // Fill marks on the right bar: one per drop it wants.
  for (int k = 1; k < node.count; ++k) {
    const float y = kGlassBottom - 2.0f -
                    (kGlassBottom - kCapBottom - 4.0f) * static_cast<float>(k) / static_cast<float>(node.count);
    canvas.Line(at(7.5f, y), at(11.0f, y), 1.1f * s, WithAlpha(art::brass::kLight, 0.55f));
  }
  // Cap, foot and finial in brass.
  art::FillConvex(canvas,
                  {at(-kCapTopHalf, 0.0f), at(kCapTopHalf, 0.0f), at(kCapHalf, kCapBottom), at(-kCapHalf, kCapBottom)},
                  art::brass::kMid);
  canvas.Line(at(-kCapTopHalf + 1.0f, 1.2f), at(kCapTopHalf - 1.0f, 1.2f), 1.4f * s, art::brass::kLight);
  canvas.Line(at(-kCapHalf + 1.0f, kCapBottom - 0.8f), at(kCapHalf - 1.0f, kCapBottom - 0.8f), 1.4f * s,
              art::brass::kDark);
  art::FillConvex(canvas,
                  {at(-kCapHalf, kGlassBottom), at(kCapHalf, kGlassBottom), at(kCapTopHalf, kFootBottom),
                   at(-kCapTopHalf, kFootBottom)},
                  art::brass::kMid);
  canvas.Line(at(-kCapHalf + 1.0f, kGlassBottom + 1.0f), at(kCapHalf - 1.0f, kGlassBottom + 1.0f), 1.3f * s,
              art::brass::kLight);
  canvas.Circle(at(0.0f, kFootBottom + 4.0f), 3.4f * s, art::brass::kShade);
  canvas.Circle(at(-0.8f, kFootBottom + 3.2f), 1.8f * s, art::brass::kLight);
  // The hanger ring between the chains.
  canvas.Ring(at(0.0f, -3.0f), 3.0f * s, 1.5f * s, art::brass::kMid);
  // Its count: what it holds of what it wants.
  char text[24];
  std::snprintf(text, sizeof(text), "%d/%d", cup.count, node.count);
  const glm::vec2 pill = at(0.0f, kPillY);
  const float text_size = 14.0f * s;
  const float width = frame.font->Measure(text, text_size) + 26.0f * s;
  const Rect box{pill - glm::vec2(width * 0.5f, 9.0f * s), pill + glm::vec2(width * 0.5f, 9.0f * s)};
  const bool bad = cup.impure || cup.spilled;
  canvas.FillRect({box.min - glm::vec2(1.0f * s), box.max + glm::vec2(1.0f * s)},
                  bad ? ui_color::kBad : Mixed(kPillInk, Accent(), 0.45f), 10.0f * s);
  canvas.FillRect(box, kPillInk, 9.0f * s);
  art::Glyph(canvas, node.want, {box.min.x + 11.0f * s, pill.y}, 3.6f * s, LiquidColor(*frame.palette, node.want));
  canvas.Text(*frame.font, text, {pill.x + 6.0f * s, pill.y + 0.5f * s}, text_size, Align::Center,
              TextStyle{.color = bad ? ui_color::kBad : Hex(0xF2F7FF)});
}

void LanternArt::DrawFront(Canvas& canvas, const ArtFrame& frame) {
  Prepare(frame);
  const Level& level = *frame.level;
  const float s = frame.Scale();
  // Brass where glass meets a part: collars at crucibles and lanterns, fittings at both ends of a coil,
  // a nozzle and a funnel round a drip fall.
  auto across = [](glm::vec2 d) { return std::atan2(d.y, d.x) + kPi * 0.5f; };
  // The tubes are measured at scroll 0.
  const glm::vec2 off = frame.layout.ScrollOffset();
  for (size_t p = 0; p < tubes_.size(); ++p) {
    const Tube& tube = tubes_[p];
    const Node& from = level.nodes[static_cast<size_t>(level.pipes[p].from)];
    const Node& to = level.nodes[static_cast<size_t>(level.pipes[p].to)];
    const float first = tube.glass.empty() ? 0.0f : tube.glass.front().x;
    const float last = tube.glass.empty() ? 0.0f : tube.glass.back().y;
    if (!tube.glass.empty() && tube.collar_start && from.kind != NodeKind::Cross && from.kind != NodeKind::Merge) {
      const glm::vec2 d = tube.whole.DirectionAt(first);
      art::BrassBar(canvas, tube.whole.PointAt(first) + off + d * (2.0f * s), glm::vec2(kTubeRadius + 1.5f, 3.0f) * s,
                    across(d));
    }
    if (!tube.glass.empty() && tube.collar_end && to.kind != NodeKind::Cross && to.kind != NodeKind::Merge) {
      const glm::vec2 d = tube.whole.DirectionAt(last);
      art::BrassBar(canvas, tube.whole.PointAt(last) + off - d * (2.5f * s), glm::vec2(kTubeRadius + 1.5f, 3.5f) * s,
                    across(d));
    }
    if (!tube.coil.empty()) {
      // Fittings across the coil's axis where it starts and ends.
      const glm::vec2 axis = glm::normalize(tube.coil.back() - tube.coil.front());
      for (const glm::vec2 at :
           {tube.coil.front() + off - axis * (2.0f * s), tube.coil.back() + off + axis * (2.0f * s)})
        art::BrassBar(canvas, at, glm::vec2(kTubeRadius + 1.5f, 3.0f) * s, across(axis));
    }
    if (tube.drip.y > tube.drip.x) {
      // The nozzle: a brass tip the drops leave from.
      const glm::vec2 top = tube.whole.PointAt(tube.drip.x) + off, d = tube.whole.DirectionAt(tube.drip.x);
      const glm::vec2 side = Left(d);
      art::BrassBar(canvas, top - d * (2.5f * s), glm::vec2(kTubeRadius + 1.5f, 3.2f) * s, across(d));
      art::FillConvex(canvas,
                      {top - side * (6.0f * s), top + side * (6.0f * s), top + d * (6.0f * s) + side * (2.5f * s),
                       top + d * (6.0f * s) - side * (2.5f * s)},
                      art::brass::kShade);
      // The funnel: wide brass lips that catch them and narrow into the glass below.
      const glm::vec2 bottom = tube.whole.PointAt(tube.drip.y) + off, e = tube.whole.DirectionAt(tube.drip.y);
      const glm::vec2 side_e = Left(e);
      const glm::vec2 lip = bottom - e * (12.0f * s);
      art::FillConvex(canvas,
                      {lip - side_e * (15.0f * s), lip + side_e * (15.0f * s), bottom + side_e * (kTubeRadius * s),
                       bottom - side_e * (kTubeRadius * s)},
                      art::brass::kMid);
      art::FillConvex(canvas,
                      {lip + side_e * (2.0f * s), lip + side_e * (15.0f * s), bottom + side_e * (kTubeRadius * s),
                       bottom + side_e * (1.0f * s)},
                      art::brass::kShade);
      canvas.Line(lip - side_e * (16.0f * s), lip + side_e * (16.0f * s), 3.0f * s, art::brass::kLight);
      canvas.Line(lip - side_e * (10.0f * s) + e * (3.0f * s), lip + side_e * (10.0f * s) + e * (3.0f * s), 2.0f * s,
                  Hex(0x2A1A08, 0.8f));
    }
  }
  for (size_t n = 0; n < level.nodes.size(); ++n) {
    const Node& node = level.nodes[n];
    const int i = static_cast<int>(n);
    const glm::vec2 c = frame.At(i);
    switch (node.kind) {
      case NodeKind::Spout: DrawCrucible(canvas, frame, i, true); break;
      case NodeKind::Cup: DrawLantern(canvas, frame, i, true); break;
      case NodeKind::Cross:
      case NodeKind::Merge: DrawGlobe(canvas, frame, i, true); break;
      case NodeKind::Fork:
      case NodeKind::Pass:
        // The socket's brass housing round the glass (a piece sits in it).
        if (node.socket || node.kind == NodeKind::Fork) {
          art::BrassRing(canvas, c, 18.5f * s, 4.5f * s);
          for (int k = 0; k < 4; ++k) {
            const float a = kPi * (0.25f + 0.5f * static_cast<float>(k));
            canvas.Circle(c + glm::vec2(std::cos(a), std::sin(a)) * (18.5f * s), 1.1f * s, art::brass::kRivet);
          }
        }
        break;
      case NodeKind::Drain: {
        // A grate over the cullet bin: what goes down here is melted again.
        canvas.Circle(c, 14.0f * s, Hex(0x05080C, 0.9f));
        art::BrassRing(canvas, c, 13.0f * s, 3.5f * s);
        for (float dy : {-5.0f, 0.0f, 5.0f}) {
          const float half = std::sqrt(std::max(0.0f, 100.0f - dy * dy));
          canvas.Line(c + glm::vec2(-half, dy) * s, c + glm::vec2(half, dy) * s, 1.8f * s, art::brass::kShade);
        }
        break;
      }
      case NodeKind::Rack:
      case NodeKind::Mill:
      case NodeKind::Gate: DrawMachinePart(canvas, frame, i); break;
    }
  }
}

wlw::ui::Rect LanternArt::CupPool(const ArtFrame& frame, int cup) const {
  const glm::vec2 top = frame.At(cup);
  const float s = frame.Scale();
  return {top + glm::vec2(-kLanternHalf + 2.5f, kCapBottom + 1.0f) * s,
          top + glm::vec2(kLanternHalf - 2.5f, kGlassBottom - 1.5f) * s};
}

void LanternArt::SpoutLiquid(Liquid& liquid, const ArtFrame& frame, int spout) const {
  // The melt in the crucible: the colours of the next drop (a blend: half and half).
  const Node& node = frame.level->nodes[static_cast<size_t>(spout)];
  const int left = static_cast<int>(node.beads.size()) - (frame.board->Started() ? frame.board->Poured(spout) : 0);
  if (left <= 0) return;
  const Bead& next = node.beads[node.beads.size() - static_cast<size_t>(left)];
  const glm::vec2 c = frame.At(spout);
  const float s = frame.Scale();
  for (int k = 0; k < 9; ++k) {
    const float u = static_cast<float>(k) / 8.0f * 2.0f - 1.0f;
    const float wave = frame.calm ? 0.0f : std::sin(frame.time * 2.4f + static_cast<float>(k) * 1.3f) * 0.8f;
    const glm::vec2 p = c + glm::vec2(u * 21.0f, kSurface + wave) * s;
    const Rgba color = LiquidColor(*frame.palette, next.IsBlend() && u > 0.0f ? next.b : next.a);
    liquid.Blob(p, {1.0f, 0.0f}, 8.0f * s, color, color, static_cast<uint32_t>(9000 + spout * 16 + k));
  }
}

glm::vec2 LanternArt::CupTop(const ArtFrame& frame, int cup) const {
  return frame.At(cup) + glm::vec2(0.0f, -kNozzleGap - 10.0f) * frame.Scale();
}

void LanternArt::DrawMud(Canvas& canvas, glm::vec2 c, float age, float s, float time) const {
  // The light chokes: a dull core, and grey smoke rising out of the glass.
  const float pop = std::min(1.0f, age * 5.0f);
  canvas.Circle(c, 12.0f * s * pop, Mixed(kMud, kSmoke, 0.45f));
  for (int k = 0; k < 7; ++k) {
    const float phase = std::fmod(age * 0.45f + static_cast<float>(k) / 7.0f, 1.0f);
    const float sway = std::sin(time * 1.3f + static_cast<float>(k) * 2.1f) * (4.0f + 8.0f * phase);
    const glm::vec2 p = c + glm::vec2(sway, -6.0f - phase * 46.0f) * s;
    canvas.Circle(p, (5.0f + phase * 11.0f) * s * pop, WithAlpha(kSmoke, 0.5f * (1.0f - phase) * pop));
  }
}

}  // namespace threadlock
