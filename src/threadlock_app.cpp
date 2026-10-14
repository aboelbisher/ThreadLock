#include "threadlock_app.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>

#include "wlw/core/log.h"
#include "wlw/ui/ui_context.h"

namespace threadlock {
namespace {

using wlw::core::Color;
using T = TwoCups;

constexpr float kTick = 0.125f;      // seconds per rules tick: a blob moves one cell (10 board units)
constexpr int kBlobDots = 40;        // dots per blob at full density
constexpr float kBlobLength = 16.0f;
constexpr float kDotSize = 13.0f;
constexpr float kLift = 56.0f;       // a dragged piece floats this far above the finger
constexpr glm::vec2 kBoardSize{360.0f, 760.0f};

// ---- Layout, board units (y down). Matches the concept mock. ----
constexpr glm::vec2 kSource{180.0f, 112.0f};
constexpr float kCupTop = 566.0f, kCupBottom = 650.0f, kCupHalf = 38.0f;
constexpr float kCupX[2] = {270.0f, 90.0f};  // blue cup (right), amber cup (left): swapped on purpose
constexpr glm::vec2 kFlowButton{300.0f, 712.0f};
constexpr float kFlowRadius = 32.0f;
constexpr int kSockets[] = {T::kSplit, T::kTank, T::kCross};
constexpr Piece kPieces[] = {Piece::Filter, Piece::Turnstile, Piece::Tank};
constexpr glm::vec2 kTraySlots[] = {{62.0f, 712.0f}, {136.0f, 712.0f}, {210.0f, 712.0f}};

// Per rules segment: its centre line. The crossing's in- and out-segments stop at its edge
// (radius 15): inside it, each lane has its own short path (kThrough).
const std::vector<glm::vec2> kSegmentPoints[] = {
    {{180, 144}, {180, 210}},                          // kFeed
    {{180, 210}, {90, 262}, {90, 362}, {168, 421}},    // kBlueToCross
    {{180, 210}, {270, 262}, {270, 312}},              // kAmberToTank
    {{270, 312}, {270, 362}, {192, 421}},              // kTankToCross
    {{192, 439}, {270, 498}, {270, kCupTop}},          // kCrossToBlue
    {{168, 439}, {90, 498}, {90, kCupTop}},            // kCrossToAmber
};
const std::vector<glm::vec2> kThrough[] = {{{168, 421}, {192, 439}}, {{192, 421}, {168, 439}}};  // blue, amber lane

const Color kBlue{0.24f, 0.63f, 1.0f, 1.0f};
const Color kAmber{1.0f, 0.67f, 0.16f, 1.0f};
const Color kMud{0.55f, 0.36f, 0.2f, 1.0f};
const Color kInk{0.83f, 0.89f, 0.95f, 1.0f};
const Color kBackground{0.027f, 0.043f, 0.063f, 1.0f};
const Color kPipeEdge{0.075f, 0.13f, 0.18f, 1.0f};
const Color kPipeInside{0.04f, 0.07f, 0.1f, 1.0f};
const Color kPanel{0.05f, 0.08f, 0.11f, 1.0f};
const Color kGood{0.62f, 0.89f, 0.69f, 1.0f};
const Color kMudText{0.88f, 0.57f, 0.35f, 1.0f};

Color WithAlpha(Color c, float a) { return {c.r, c.g, c.b, c.a * a}; }

Color LiquidColor(Liquid liquid) {
  switch (liquid) {
    case Liquid::Blue: return kBlue;
    case Liquid::Amber: return kAmber;
    default: return kMud;
  }
}

std::vector<glm::vec2> ToWorld(const std::vector<glm::vec2>& board) {
  std::vector<glm::vec2> world;
  for (const glm::vec2& p : board) world.push_back({p.x, -p.y});
  return world;
}

std::vector<glm::vec2> Circle(glm::vec2 center, float radius, int points) {
  std::vector<glm::vec2> loop;
  for (int i = 0; i <= points; ++i) {
    const float a = 2.0f * std::numbers::pi_v<float> * static_cast<float>(i) / static_cast<float>(points);
    loop.push_back({center.x + radius * std::cos(a), center.y + radius * std::sin(a)});
  }
  return loop;
}

}  // namespace

wlw::app::AppConfig ThreadlockApp::MakeConfig() {
  wlw::app::AppConfig config;
  config.title = "Threadlock";
  config.window_size = {450, 950};
  config.ui_reference_size = {kBoardSize.x, kBoardSize.y + 40.0f};  // the board + room for the status bar
  return config;
}

void ThreadlockApp::OnStart() {
  using namespace wlw;
  font_ = ui::Font::Load("fonts/LilitaOne-Regular.ttf");

  flow_ = std::make_unique<fx::FlowParticles>(8000);
  flow_->speed_jitter = 0.04f;  // a blob stays together
  flow_->fade_length = 6.0f;    // dots shrink into a junction and grow out of the next one
  dots_ = std::make_shared<scene::SpriteBatch>(8000);

  // One path per rules segment, at the speed that crosses it in its number of ticks.
  const Level& level = board_.GetLevel();
  const fx::FlowPathSettings pipe{.width = 7.0f, .speed = 1.0f, .packed_per_unit = 3.0f, .wobble = 0.35f, .wobble_hz = 0.7f};
  for (size_t s = 0; s < level.segments.size(); ++s) {
    const int path = flow_->AddPath(ToWorld(kSegmentPoints[s]), pipe);
    flow_->SetSpeed(path, flow_->PathLength(path) / (static_cast<float>(level.segments[s].cells) * kTick));
    segment_path_.push_back(path);
  }
  segment_stopped_.assign(segment_path_.size(), false);
  const float cross_seconds = static_cast<float>(level.nodes[T::kCross].dwell) * kTick;
  for (int lane = 0; lane < 2; ++lane) {
    through_path_[lane] = flow_->AddPath(ToWorld(kThrough[lane]), pipe);
    flow_->SetSpeed(through_path_[lane], flow_->PathLength(through_path_[lane]) / cross_seconds);
  }
  // A cup is a wide path from its rim to its bottom with a stop at the end: blobs queue up from
  // the bottom, so the cup fills.
  for (int cup = 0; cup < 2; ++cup) {
    const std::vector<glm::vec2> line = {{kCupX[cup], kCupTop + 2.0f}, {kCupX[cup], kCupBottom - 7.0f}};
    const float per_unit = static_cast<float>(kBlobDots * level.cup_needs) / (kCupBottom - kCupTop - 12.0f);
    cup_path_[cup] = flow_->AddPath(ToWorld(line), {.width = 56.0f, .speed = 90.0f, .packed_per_unit = per_unit, .wobble = 0.1f});
    flow_->SetStop(cup_path_[cup], flow_->PathLength(cup_path_[cup]));
  }
  // The source: two loops (each routed back into itself) swirling blue and amber together.
  const float radii[2] = {13.0f, 7.0f};
  for (int i = 0; i < 2; ++i) {
    swirl_path_[i] = flow_->AddPath(ToWorld(Circle(kSource, radii[i], 24)), {.width = 6.0f, .speed = 26.0f - 8.0f * i});
    const int self[] = {swirl_path_[i]};
    flow_->SetNext(swirl_path_[i], self);
  }

  camera_ = scene::OrthographicCamera::Create();
  liquid_ = scene::Viewport::Create({.name = "Liquid", .format = rendering::TextureFormat::RGBA16F, .depth = false});
  liquid_->SetCamera(camera_);
  liquid_->GetScene()->AddNode(dots_);
  scene::AddBlur(*liquid_, {.scale = 1.0f, .spread = 1.0f, .format = rendering::TextureFormat::RGBA16F});
  scene::AddThreshold(*liquid_, {.threshold = 0.35f, .softness = 0.05f});
  App().Window().AddViewport(liquid_);

  scene::Viewport& screen = App().Window().GetScreen();
  screen.SetClearColor(kBackground);
  picture_ = &screen.AddCanvasLayer({.mode = scene::CanvasMode::Picture});  // pipes + liquid: bloomed
  bloom_ = scene::AddBloom(screen, {.threshold = 0.6f, .strength = 0.6f});

  App().Quality().Configure({.levels = 3});
  App().Quality().OnChange([this](int level) { ApplyQuality(level); });  // also once now
  ResetLiquid();
}

void ThreadlockApp::ApplyQuality(int level) {
  struct Tier {
    float density, liquid, bloom;
  };
  static constexpr Tier kTiers[] = {{0.3f, 0.35f, 0.125f}, {0.6f, 0.5f, 0.25f}, {1.0f, 0.5f, 0.25f}};
  const Tier& tier = kTiers[std::clamp(level, 0, 2)];
  density_ = tier.density;
  flow_->SetDensity(tier.density);
  flow_->dot_size = kDotSize / std::sqrt(std::sqrt(tier.density));  // fewer dots, a bit bigger
  liquid_->SetSizeFromScreen(tier.liquid);
  bloom_.SetScale(App().Window().GetScreen(), tier.bloom);
  WLW_LOG_INFO("Quality level %d: density %.1f, dot %.1f, liquid %.2f, bloom %.3f", level, tier.density,
               flow_->dot_size, tier.liquid, tier.bloom);
}

void ThreadlockApp::ResetLiquid() {
  flow_->Clear();
  const Color colors[2] = {kBlue, kAmber};
  for (int i = 0; i < 2; ++i) {
    const int n = static_cast<int>(70.0f * density_);
    const float length = flow_->PathLength(swirl_path_[i]);
    for (int d = 0; d < n; ++d) flow_->Emit(swirl_path_[i], length * static_cast<float>(d) / n, colors[(d + i) % 2]);
  }
  for (size_t s = 0; s < segment_path_.size(); ++s) {
    flow_->SetStop(segment_path_[s], wlw::fx::FlowParticles::kOpen);
    segment_stopped_[s] = false;
  }
}

void ThreadlockApp::StartRun() {
  ResetLiquid();
  board_.Start();
  tick_clock_ = 0.0f;
  collide_flash_ = 0.0f;
  phase_ = Phase::Run;
}

void ThreadlockApp::StopRun() {
  ResetLiquid();
  phase_ = Phase::Edit;
}

void ThreadlockApp::OnFixedUpdate(float dt) {
  if (phase_ != Phase::Run) return;
  tick_clock_ += dt;
  while (tick_clock_ >= kTick && phase_ == Phase::Run) {
    tick_clock_ -= kTick;
    HandleEvents(board_.Tick());
    UpdateStops();
    if (board_.Finished()) {
      phase_ = Phase::Done;
      WLW_LOG_INFO("Run finished after %d ticks: %s", board_.Ticks(), board_.Solved() ? "solved" : "mud");
    }
  }
}

void ThreadlockApp::Burst(int path, Liquid liquid) {
  const int n = std::max(6, static_cast<int>(std::lround(kBlobDots * density_)));
  for (int i = 0; i < n; ++i) {
    const Color color = liquid == Liquid::Mixed ? (i % 2 ? kAmber : kBlue) : LiquidColor(liquid);
    flow_->Emit(path, kBlobLength * static_cast<float>(i) / static_cast<float>(n), color);
  }
}

// Junctions re-emit (like Niagara's event handlers: on arrival, spawn in another emitter): a
// blob's dots fade out at the end of one path, under a piece, and new dots start on the next path
// in the colour the rules say.
void ThreadlockApp::HandleEvents(const std::vector<Event>& events) {
  bool collided = false;
  for (const Event& e : events) collided |= e.type == EventType::Collide;
  if (collided) collide_flash_ = 1.0f;

  for (const Event& e : events) {
    switch (e.type) {
      case EventType::EnterSegment:
        Burst(segment_path_[static_cast<size_t>(e.segment)], e.liquid);
        break;
      case EventType::EnterNode:
        if (e.node == T::kCross) {
          const Piece piece = board_.PieceAt(T::kCross);
          if (piece == Piece::Turnstile) spin_target_ += std::numbers::pi_v<float> / 2.0f;
          if (piece != Piece::Tank) {  // a tank there holds it: it shows up again on the way out
            const int lane = e.segment == T::kBlueToCross ? 0 : 1;
            Burst(through_path_[lane], collided ? Liquid::Mud : e.liquid);
          }
        } else if (e.node == T::kCupBlue || e.node == T::kCupAmber) {
          Burst(cup_path_[e.node == T::kCupBlue ? 0 : 1], e.liquid);
        }
        break;
      default:
        break;
    }
  }
}

// A blob stuck at a turnstile or held by a tank: its dots pile up at the end of its path.
void ThreadlockApp::UpdateStops() {
  for (size_t s = 0; s < segment_path_.size(); ++s) {
    const bool blocked = board_.Blocked(static_cast<int>(s));
    if (blocked == segment_stopped_[s]) continue;
    segment_stopped_[s] = blocked;
    const int path = segment_path_[s];
    flow_->SetStop(path, blocked ? flow_->PathLength(path) - 2.0f : wlw::fx::FlowParticles::kOpen);
  }
}

void ThreadlockApp::OnUpdate(float dt) {
  time_ += dt;
  collide_flash_ = std::max(0.0f, collide_flash_ - dt * 1.5f);
  spin_ += (spin_target_ - spin_) * std::min(1.0f, dt * 10.0f);
  flow_->Update(dt);
  flow_->WriteTo(*dots_);
}

// ---- Input ----

glm::vec2 ThreadlockApp::SocketPos(int node) const {
  switch (node) {
    case T::kSplit: return {180.0f, 210.0f};
    case T::kTank: return {270.0f, 312.0f};
    default: return {180.0f, 430.0f};
  }
}

glm::vec2 ThreadlockApp::PiecePos(Piece piece) const {
  const int node = board_.NodeOf(piece);
  if (node >= 0) return SocketPos(node);
  return kTraySlots[static_cast<int>(piece) - 1];
}

int ThreadlockApp::SocketNear(glm::vec2 board) const {
  int best = -1;
  float best_distance = 52.0f;
  for (int node : kSockets) {
    const float d = glm::distance(board, SocketPos(node));
    if (d < best_distance) {
      best_distance = d;
      best = node;
    }
  }
  return best;
}

void ThreadlockApp::HandleInput(const wlw::ui::UIContext& ui) {
  const glm::vec2 p = ui.pointer.position - offset_;
  if (ui.pointer.pressed && !dragging_) {
    if (glm::distance(p, kFlowButton) < kFlowRadius + 12.0f) {
      if (phase_ == Phase::Edit) StartRun(); else StopRun();
      return;
    }
    if (phase_ == Phase::Edit) {
      for (Piece piece : kPieces) {
        if (glm::distance(p, PiecePos(piece)) > 34.0f) continue;
        drag_ = {piece, board_.NodeOf(piece), p, p};
        board_.Remove(piece);
        dragging_ = true;
        break;
      }
    }
  }
  if (!dragging_) return;
  drag_.at = p;
  if (!ui.pointer.released) return;
  dragging_ = false;
  if (glm::distance(drag_.at, drag_.start) < 10.0f) {  // a tap: it stays where it was
    if (drag_.from >= 0) board_.Place(drag_.piece, drag_.from);
    return;
  }
  const int socket = SocketNear(drag_.at - glm::vec2(0.0f, kLift));
  if (socket >= 0) board_.Place(drag_.piece, socket);  // else it goes back to the tray
}

// ---- Drawing ----

void ThreadlockApp::OnRenderUI(wlw::ui::UIContext& ui) {
  const glm::vec2 size = ui.canvas.Size();
  const wlw::ui::Rect& safe = ui.safe_area;
  offset_ = {(size.x - kBoardSize.x) * 0.5f,
             safe.min.y + std::max(0.0f, (safe.max.y - safe.min.y - kBoardSize.y) * 0.5f)};
  // The liquid's camera sees canvas units, so dots line up with the pieces (world = (x, -y)).
  camera_->SetVisibleArea(size.x, size.y);
  const glm::vec2 eye{size.x * 0.5f - offset_.x, -(size.y * 0.5f - offset_.y)};
  camera_->SetPosition({eye.x, eye.y, 10.0f});
  camera_->SetTarget({eye.x, eye.y, 0.0f});

  HandleInput(ui);
  DrawBackground(*picture_);
  DrawBoard(ui.canvas);
}

namespace {
void Polyline(wlw::ui::Canvas& canvas, const std::vector<glm::vec2>& points, glm::vec2 offset, float thickness,
              const Color& color) {
  for (size_t i = 1; i < points.size(); ++i) canvas.Line(points[i - 1] + offset, points[i] + offset, thickness, color);
}
}  // namespace

void ThreadlockApp::DrawBackground(wlw::ui::Canvas& canvas) {
  for (int pass = 0; pass < 2; ++pass) {
    const float thickness = pass == 0 ? 24.0f : 17.0f;
    const Color& color = pass == 0 ? kPipeEdge : kPipeInside;
    for (const auto& points : kSegmentPoints) Polyline(canvas, points, offset_, thickness, color);
    for (const auto& points : kThrough) Polyline(canvas, points, offset_, thickness, color);
  }
  canvas.Circle(ToCanvas(kSource), 34.0f, kPipeInside);
  const int tank = board_.NodeOf(Piece::Tank);
  if (tank >= 0) {
    const glm::vec2 c = ToCanvas(SocketPos(tank));
    canvas.FillRect({c - glm::vec2(22.0f, 32.0f), c + glm::vec2(22.0f, 32.0f)}, kPipeInside, 9.0f);
  }
  for (float x : kCupX) {
    canvas.FillRect({ToCanvas({x - kCupHalf, kCupTop}), ToCanvas({x + kCupHalf, kCupBottom})}, kPipeInside);
  }
  if (const auto& texture = liquid_->GetTexture()) {
    wlw::ui::ImageStyle style;
    style.uv = {{0.0f, 1.0f}, {1.0f, 0.0f}};  // render targets are bottom-up
    canvas.Image({{0.0f, 0.0f}, canvas.Size()}, texture, style);
  }
}

void ThreadlockApp::DrawPiece(wlw::ui::Canvas& canvas, Piece piece, glm::vec2 board, float scale, float alpha,
                              float spin) {
  const glm::vec2 c = ToCanvas(board);
  const Color ink = WithAlpha(kInk, alpha);
  switch (piece) {
    case Piece::Filter:
      canvas.Circle(c, 22.0f * scale, WithAlpha(kPanel, alpha));
      canvas.Ring(c, 22.0f * scale, 2.5f * scale, ink);
      canvas.Line(c + glm::vec2(0.0f, -10.0f) * scale, c, 4.0f * scale, WithAlpha(kBlue, alpha));
      canvas.Line(c, c + glm::vec2(-9.0f, 9.0f) * scale, 4.0f * scale, WithAlpha(kBlue, alpha));
      canvas.Line(c, c + glm::vec2(9.0f, 9.0f) * scale, 4.0f * scale, WithAlpha(kAmber, alpha));
      break;
    case Piece::Turnstile:
      canvas.Circle(c, 25.0f * scale, WithAlpha(kPanel, 0.6f * alpha));
      canvas.Ring(c, 25.0f * scale, 2.5f * scale, ink);
      for (int arm = 0; arm < 4; ++arm) {
        const float a = spin + std::numbers::pi_v<float> * (0.25f + 0.5f * static_cast<float>(arm));
        canvas.Line(c, c + glm::vec2(std::cos(a), std::sin(a)) * 23.0f * scale, 4.0f * scale, ink);
      }
      canvas.Circle(c, 5.5f * scale, ink);
      break;
    case Piece::Tank: {
      const glm::vec2 h = glm::vec2(22.0f, 32.0f) * scale;
      const glm::vec2 corners[] = {c - h, {c.x + h.x, c.y - h.y}, c + h, {c.x - h.x, c.y + h.y}};
      for (int i = 0; i < 4; ++i) canvas.Line(corners[i], corners[(i + 1) % 4], 2.5f * scale, ink);
      for (int i = 1; i < 4; ++i) {
        const float y = c.y - h.y + 16.0f * scale * static_cast<float>(i);
        canvas.Line({c.x + 13.0f * scale, y}, {c.x + 19.0f * scale, y}, 1.5f * scale, WithAlpha(kInk, 0.4f * alpha));
      }
      break;
    }
    case Piece::None:
      break;
  }
}

void ThreadlockApp::DrawBoard(wlw::ui::Canvas& canvas) {
  using wlw::ui::Align;
  using wlw::ui::TextStyle;
  // The source.
  canvas.Ring(ToCanvas(kSource), 34.0f, 2.5f, WithAlpha(kInk, 0.55f));

  // Cups: an open glass, ticks for each blob it needs, the count beside it.
  const int needs = board_.GetLevel().cup_needs;
  for (int i = 0; i < 2; ++i) {
    const Cup& cup = board_.CupAt(i == 0 ? T::kCupBlue : T::kCupAmber);
    const Color color = cup.mud ? kMud : (i == 0 ? kBlue : kAmber);
    const float x = kCupX[i];
    const glm::vec2 corners[] = {{x - kCupHalf, kCupTop}, {x - kCupHalf, kCupBottom}, {x + kCupHalf, kCupBottom},
                                 {x + kCupHalf, kCupTop}};
    for (int k = 0; k < 3; ++k) canvas.Line(ToCanvas(corners[k]), ToCanvas(corners[k + 1]), 3.0f, color);
    for (int k = 1; k < needs; ++k) {
      const float y = kCupBottom - 4.0f - (kCupBottom - kCupTop - 8.0f) * static_cast<float>(k) / static_cast<float>(needs);
      canvas.Line(ToCanvas({x + kCupHalf - 9.0f, y}), ToCanvas({x + kCupHalf - 3.0f, y}), 1.5f, WithAlpha(kInk, 0.3f));
    }
    if (font_) {
      char text[16];
      std::snprintf(text, sizeof(text), "%d/%d", std::min(cup.count, needs), needs);
      const bool right = i == 0;
      canvas.Text(*font_, cup.mud ? "mud" : text, ToCanvas({right ? x + kCupHalf + 6.0f : x - kCupHalf - 6.0f, 610.0f}),
                  20.0f, right ? Align::Left : Align::Right, {.color = cup.mud ? kMudText : color});
    }
  }

  // Sockets: the piece in it, or a dashed ring (bright while a dragged piece would land there).
  const int target = dragging_ ? SocketNear(drag_.at - glm::vec2(0.0f, kLift)) : -1;
  for (int node : kSockets) {
    const Piece piece = board_.PieceAt(node);
    if (piece != Piece::None) {
      DrawPiece(canvas, piece, SocketPos(node), 1.0f, 1.0f, node == T::kCross ? spin_ : 0.0f);
      continue;
    }
    const float pulse = node == target ? 0.95f : 0.3f + 0.2f * std::sin(time_ * 3.0f);
    const float radius = node == T::kCross ? 30.0f : 27.0f;
    for (int dash = 0; dash < 12; ++dash) {
      canvas.Arc(ToCanvas(SocketPos(node)), radius, node == target ? 3.0f : 2.0f,
                 static_cast<float>(dash) * std::numbers::pi_v<float> / 6.0f, 0.3f, WithAlpha(kInk, pulse));
    }
  }
  if (collide_flash_ > 0.0f) {
    canvas.Ring(ToCanvas(SocketPos(T::kCross)), 30.0f + (1.0f - collide_flash_) * 50.0f, 4.0f,
                WithAlpha(kMudText, collide_flash_));
  }

  if (font_) {
    canvas.Text(*font_, "Two Cups", ToCanvas({24.0f, 36.0f}), 30.0f, Align::Left, {.color = kInk});
    canvas.Text(*font_, "LEVEL 1", ToCanvas({336.0f, 36.0f}), 18.0f, Align::Right, {.color = WithAlpha(kInk, 0.5f)});
    const char* message = nullptr;
    Color color = kInk;
    const bool placed = board_.NodeOf(Piece::Filter) >= 0 || board_.NodeOf(Piece::Turnstile) >= 0 ||
                        board_.NodeOf(Piece::Tank) >= 0;
    const bool mud = board_.CupAt(T::kCupBlue).mud || board_.CupAt(T::kCupAmber).mud;
    switch (phase_) {
      case Phase::Edit:
        message = placed ? "Ready? Tap play" : "Drag pieces into the sockets";
        color = placed ? kInk : WithAlpha(kInk, 0.7f);
        break;
      case Phase::Run:
        message = mud || collide_flash_ > 0.0f ? "Mud!" : "Pouring...";
        color = mud || collide_flash_ > 0.0f ? kMudText : WithAlpha(kInk, 0.7f);
        break;
      case Phase::Done:
        message = board_.Solved() ? "Solved! Both cups pure" : "Mud. Move pieces, try again";
        color = board_.Solved() ? kGood : kMudText;
        break;
    }
    canvas.Text(*font_, message, ToCanvas({180.0f, 668.0f}), 20.0f, Align::Center, {.color = color});
  }

  // Tray and the play button.
  canvas.FillRect({ToCanvas({18.0f, 684.0f}), ToCanvas({254.0f, 742.0f})}, kPanel, 18.0f);
  for (int i = 0; i < 3; ++i) {
    const Piece piece = kPieces[i];
    if (board_.NodeOf(piece) >= 0 || (dragging_ && drag_.piece == piece)) {
      canvas.Ring(ToCanvas(kTraySlots[i]), 18.0f, 1.5f, WithAlpha(kInk, 0.15f));
      continue;
    }
    DrawPiece(canvas, piece, kTraySlots[i], piece == Piece::Tank ? 0.7f : 0.9f, phase_ == Phase::Edit ? 1.0f : 0.35f, 0.0f);
  }
  const glm::vec2 b = ToCanvas(kFlowButton);
  canvas.Circle(b, kFlowRadius, phase_ == Phase::Run ? Color{0.13f, 0.19f, 0.25f, 1.0f} : kGood);
  const Color icon = phase_ == Phase::Run ? kInk : Color{0.03f, 0.08f, 0.05f, 1.0f};
  if (phase_ == Phase::Edit) {
    canvas.Triangle(b + glm::vec2(-8.0f, -12.0f), b + glm::vec2(13.0f, 0.0f), b + glm::vec2(-8.0f, 12.0f), icon);
  } else if (phase_ == Phase::Run) {
    canvas.FillRect({b - glm::vec2(9.0f), b + glm::vec2(9.0f)}, icon, 2.0f);
  } else {
    canvas.Arc(b, 11.0f, 3.5f, 0.6f, 4.6f, icon);  // back to editing
    canvas.Triangle(b + glm::vec2(0.0f, -16.0f), b + glm::vec2(9.0f, -10.0f), b + glm::vec2(0.0f, -4.0f), icon);
  }

  if (dragging_) DrawPiece(canvas, drag_.piece, drag_.at - glm::vec2(0.0f, kLift), 1.0f, 1.0f, 0.0f);
}

}  // namespace threadlock
