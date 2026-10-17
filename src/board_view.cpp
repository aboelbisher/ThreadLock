#include "board_view.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <string>

namespace threadlock {
namespace {

using wlw::ui::Align;
using wlw::ui::Canvas;
using wlw::ui::TextStyle;
constexpr float kPi = std::numbers::pi_v<float>;

glm::vec2 V(Vec2 p) { return {p.x, p.y}; }

glm::vec2 Normal(glm::vec2 v) {
  const float length = glm::length(v);
  return length > 1e-5f ? v / length : glm::vec2(0.0f, 1.0f);
}

glm::vec2 Perp(glm::vec2 v) { return {-v.y, v.x}; }

// Directions at a node, in board space: into it along each in-pipe, out of it along each out-pipe.
glm::vec2 InDir(const Level& level, const Node& node, size_t lane) {
  if (lane >= node.ins.size()) return {0.0f, 1.0f};
  const Pipe& pipe = level.pipes[static_cast<size_t>(node.ins[lane])];
  return Normal(V(node.at) - V(pipe.points.back()));
}

glm::vec2 OutDir(const Level& level, const Node& node, size_t side) {
  if (side >= node.outs.size()) return side == 0 ? glm::vec2(-0.7f, 0.7f) : glm::vec2(0.7f, 0.7f);
  const Pipe& pipe = level.pipes[static_cast<size_t>(node.outs[side])];
  return Normal(V(pipe.points.front()) - V(node.at));
}

void Polyline(Canvas& canvas, const std::vector<glm::vec2>& points, float thickness, const Rgba& color) {
  for (size_t i = 1; i < points.size(); ++i) canvas.Line(points[i - 1], points[i], thickness, color);
  for (size_t i = 1; i + 1 < points.size(); ++i) canvas.Circle(points[i], thickness * 0.5f, color);  // round joints
}

void DashedLine(Canvas& canvas, glm::vec2 a, glm::vec2 b, float thickness, const Rgba& color, int dashes) {
  for (int i = 0; i < dashes; ++i) {
    const float t0 = (static_cast<float>(i) + 0.15f) / static_cast<float>(dashes);
    const float t1 = (static_cast<float>(i) + 0.7f) / static_cast<float>(dashes);
    canvas.Line(a + (b - a) * t0, a + (b - a) * t1, thickness, color);
  }
}

void OutlineRect(Canvas& canvas, glm::vec2 min, glm::vec2 max, float thickness, const Rgba& color) {
  canvas.Line({min.x, min.y}, {max.x, min.y}, thickness, color);
  canvas.Line({max.x, min.y}, {max.x, max.y}, thickness, color);
  canvas.Line({max.x, max.y}, {min.x, max.y}, thickness, color);
  canvas.Line({min.x, max.y}, {min.x, min.y}, thickness, color);
}

// A shape for each colour (backing the colour up for colour-blind players).
void Glyph(Canvas& canvas, Color color, glm::vec2 c, float r, const Rgba& fill) {
  switch (color) {
    case Color::Blue: canvas.Circle(c, r, fill); break;
    case Color::Amber:
      canvas.Triangle(c + glm::vec2(0.0f, -r * 1.1f), c + glm::vec2(r, r * 0.8f), c + glm::vec2(-r, r * 0.8f), fill);
      break;
    case Color::Green:
      canvas.Triangle(c + glm::vec2(0.0f, -r * 1.15f), c + glm::vec2(r * 1.15f, 0.0f), c + glm::vec2(0.0f, r * 1.15f),
                      fill);
      canvas.Triangle(c + glm::vec2(0.0f, -r * 1.15f), c + glm::vec2(0.0f, r * 1.15f), c + glm::vec2(-r * 1.15f, 0.0f),
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

void DrawBead(Canvas& canvas, const threadlock::Bead& bead, glm::vec2 c, float r, const Palette& palette, float alpha) {
  canvas.Circle(c, r, WithAlpha(LiquidColor(palette, bead.a), alpha));
  if (bead.IsBlend()) canvas.Arc(c, r * 0.5f, r, 0.0f, kPi, WithAlpha(LiquidColor(palette, bead.b), alpha));
}

void Padlock(Canvas& canvas, glm::vec2 c, float s, const Rgba& color) {
  canvas.Arc(c + glm::vec2(0.0f, -3.0f * s), 4.5f * s, 2.2f * s, -kPi * 0.5f, kPi, color);
  canvas.FillRect({c + glm::vec2(-6.5f, -2.5f) * s, c + glm::vec2(6.5f, 7.0f) * s}, color, 2.0f * s);
}

void Sparkle(Canvas& canvas, glm::vec2 c, float r, const Rgba& color) {
  canvas.Triangle(c + glm::vec2(0.0f, -r), c + glm::vec2(r * 0.22f, 0.0f), c + glm::vec2(-r * 0.22f, 0.0f), color);
  canvas.Triangle(c + glm::vec2(0.0f, r), c + glm::vec2(-r * 0.22f, 0.0f), c + glm::vec2(r * 0.22f, 0.0f), color);
  canvas.Triangle(c + glm::vec2(-r, 0.0f), c + glm::vec2(0.0f, -r * 0.22f), c + glm::vec2(0.0f, r * 0.22f), color);
  canvas.Triangle(c + glm::vec2(r, 0.0f), c + glm::vec2(0.0f, r * 0.22f), c + glm::vec2(0.0f, -r * 0.22f), color);
}

constexpr float kCupHalf = 24.0f, kCupHeight = 62.0f;

}  // namespace

// ---- Pieces ----

void DrawPiece(Canvas& canvas, const Piece& piece, glm::vec2 c, float s, const PieceLook& look, const Palette& palette,
               const Level* level, const Node* node, const wlw::ui::Font* font) {
  const Rgba ink = WithAlpha(ui_color::kInk, look.alpha);
  const Rgba panel = WithAlpha(ui_color::kPanel, 0.82f * look.alpha);
  const bool on_board = level && node;
  auto puck = [&](float r) {
    canvas.Circle(c, r * s, panel);
    canvas.Ring(c, r * s, 2.5f * s, ink);
  };
  auto out_dir = [&](int side) {
    return on_board ? OutDir(*level, *node, static_cast<size_t>(side))
                    : (side == 0 ? glm::vec2(-0.7f, 0.7f) : glm::vec2(0.7f, 0.7f));
  };
  auto in_dir = [&](int lane) {
    if (on_board) return InDir(*level, *node, static_cast<size_t>(lane));
    return lane == 0 ? glm::vec2(0.0f, 1.0f) : glm::vec2(-1.0f, 0.0f);
  };
  switch (piece.type) {
    case PieceType::Separator: {
      puck(22.0f);
      const int side_a = look.setting == 0 ? 0 : 1;
      const glm::vec2 in = on_board ? InDir(*level, *node, 0) : glm::vec2(0.0f, 1.0f);
      canvas.Line(c - in * (11.0f * s), c, 4.0f * s, ink);
      canvas.Line(c, c + out_dir(side_a) * (12.0f * s), 4.5f * s, WithAlpha(LiquidColor(palette, piece.a), look.alpha));
      canvas.Line(c, c + out_dir(1 - side_a) * (12.0f * s), 4.5f * s,
                  WithAlpha(LiquidColor(palette, piece.b), look.alpha));
      canvas.Circle(c, 3.0f * s, ink);
      break;
    }
    case PieceType::Turnstile: {
      canvas.Circle(c, 24.0f * s, WithAlpha(ui_color::kPanel, 0.55f * look.alpha));
      canvas.Ring(c, 24.0f * s, 2.5f * s, ink);
      for (int arm = 0; arm < 4; ++arm) {
        const float a = look.spin + kPi * (0.25f + 0.5f * static_cast<float>(arm));
        canvas.Line(c, c + glm::vec2(std::cos(a), std::sin(a)) * (21.0f * s), 4.0f * s, ink);
      }
      canvas.Circle(c, 5.5f * s, ink);
      // The arrow: which lane goes first when both arrive together (it points the way that lane flows).
      const glm::vec2 d = in_dir(look.setting);
      const glm::vec2 p = Perp(d);
      const glm::vec2 tip = c - d * (27.0f * s);
      canvas.Triangle(tip, tip - d * (8.0f * s) + p * (6.0f * s), tip - d * (8.0f * s) - p * (6.0f * s),
                      WithAlpha(ui_color::kGood, look.alpha));
      break;
    }
    case PieceType::TankShort:
    case PieceType::TankTall: {
      const float h = (piece.type == PieceType::TankShort ? 46.0f : 60.0f) * s, w = 40.0f * s;
      const glm::vec2 min = c - glm::vec2(w, h) * 0.5f, max = c + glm::vec2(w, h) * 0.5f;
      canvas.FillRect({min, max}, WithAlpha(ui_color::kPanel, 0.45f * look.alpha), 8.0f * s);
      OutlineRect(canvas, min + glm::vec2(1.0f), max - glm::vec2(1.0f), 2.5f * s, ink);
      for (int i = 1; i < 4; ++i) {
        const float y = min.y + h * static_cast<float>(i) / 4.0f;
        canvas.Line({max.x - 9.0f * s, y}, {max.x - 4.0f * s, y}, 1.5f * s,
                    WithAlpha(ui_color::kInk, 0.4f * look.alpha));
      }
      if (font) {
        canvas.Text(*font, piece.type == PieceType::TankShort ? "+4" : "+8",
                    c + glm::vec2(-3.0f * s, -h * 0.5f - 9.0f * s), 13.0f * s, Align::Center,
                    TextStyle::Outlined(ink, ui_color::kBackground, look.alpha));
      }
      break;
    }
    case PieceType::Flipper: {
      puck(22.0f);
      float t = look.arm >= 0.0f ? look.arm : static_cast<float>(look.setting);
      t = std::clamp(t, 0.0f, 1.0f);
      const glm::vec2 dir = Normal(out_dir(0) * (1.0f - t) + out_dir(1) * t);
      canvas.Line(c - dir * (5.0f * s), c + dir * (14.0f * s), 4.5f * s, ink);
      canvas.Circle(c, 3.5f * s, ink);
      const glm::vec2 other = Normal(out_dir(0) * t + out_dir(1) * (1.0f - t));
      canvas.Ring(c + other * (12.0f * s), 3.0f * s, 1.5f * s, WithAlpha(ui_color::kInk, 0.5f * look.alpha));
      break;
    }
    case PieceType::Overflow: {
      const glm::vec2 h(21.0f * s);
      canvas.FillRect({c - h, c + h}, panel, 10.0f * s);
      OutlineRect(canvas, c - h + glm::vec2(1.0f), c + h - glm::vec2(1.0f), 2.5f * s, ink);
      canvas.Line(c + glm::vec2(-12.0f, -10.0f) * s, c + glm::vec2(-12.0f, 8.0f) * s, 2.5f * s, ink);
      canvas.Line(c + glm::vec2(-12.0f, 8.0f) * s, c + glm::vec2(12.0f, 8.0f) * s, 2.5f * s, ink);
      canvas.Line(c + glm::vec2(12.0f, 8.0f) * s, c + glm::vec2(12.0f, -10.0f) * s, 2.5f * s, ink);
      if (font) {
        const int n = look.remaining >= 0 ? look.remaining : piece.n;
        canvas.Text(*font, std::to_string(n), c + glm::vec2(0.0f, -1.0f * s), 16.0f * s, Align::Center,
                    TextStyle::Outlined(ink, ui_color::kBackground, look.alpha));
      }
      // Its way: an arrow out of the side the first N go.
      const glm::vec2 d = out_dir(look.setting);
      const glm::vec2 tip = c + d * (34.0f * s);
      canvas.Triangle(tip, tip - d * (8.0f * s) + Perp(d) * (5.5f * s), tip - d * (8.0f * s) - Perp(d) * (5.5f * s),
                      WithAlpha(ui_color::kGood, look.alpha));
      break;
    }
    case PieceType::Bridge: {
      puck(22.0f);
      canvas.Arc(c + glm::vec2(0.0f, 12.0f * s), 19.0f * s, 4.0f * s, -1.0f, 2.0f, ink);
      canvas.Line(c + glm::vec2(-17.0f, 8.0f) * s, c + glm::vec2(17.0f, 8.0f) * s, 2.5f * s, ink);
      canvas.Line(c + glm::vec2(-9.0f, 8.0f) * s, c + glm::vec2(-9.0f, 0.0f) * s, 2.0f * s, ink);
      canvas.Line(c + glm::vec2(9.0f, 8.0f) * s, c + glm::vec2(9.0f, 0.0f) * s, 2.0f * s, ink);
      break;
    }
    case PieceType::Mixer: {
      puck(22.0f);
      canvas.Arc(c, 11.0f * s, 4.0f * s, look.spin, 2.0f, WithAlpha(palette.blue, look.alpha));
      canvas.Arc(c, 11.0f * s, 4.0f * s, look.spin + kPi, 2.0f, WithAlpha(palette.amber, look.alpha));
      canvas.Circle(c, 4.5f * s, WithAlpha(palette.green, look.alpha));
      break;
    }
    case PieceType::TwinGate: {
      const glm::vec2 h(24.0f * s, 16.0f * s);
      canvas.FillRect({c - h, c + h}, panel, 8.0f * s);
      OutlineRect(canvas, c - h + glm::vec2(1.0f), c + h - glm::vec2(1.0f), 2.5f * s, ink);
      canvas.Line(c + glm::vec2(-10.0f, -10.0f) * s, c + glm::vec2(-10.0f, 10.0f) * s, 4.0f * s, ink);
      canvas.Line(c + glm::vec2(10.0f, -10.0f) * s, c + glm::vec2(10.0f, 10.0f) * s, 4.0f * s, ink);
      DashedLine(canvas, c + glm::vec2(-10.0f, 0.0f) * s, c + glm::vec2(10.0f, 0.0f) * s, 2.0f * s, ink, 3);
      break;
    }
  }
  if (look.locked) Padlock(canvas, c + glm::vec2(19.0f, -19.0f) * s, 0.8f * s, WithAlpha(ui_color::kMuted, look.alpha));
}

// ---- BoardView ----

void BoardView::SetLevel(const Level* level) {
  level_ = level;
  anims_.assign(level ? level->nodes.size() : 0, {});
  cup_fill_.assign(level ? level->nodes.size() : 0, 0.0f);
  bottle_fill_.assign(level ? level->nodes.size() : 0, {});
  for (size_t n = 0; level && n < level->nodes.size(); ++n) bottle_fill_[n].assign(level->nodes[n].slots.size(), 0.0f);
  markers_.clear();
}

void BoardView::ClearMarkers() {
  markers_.clear();
  std::fill(cup_fill_.begin(), cup_fill_.end(), 0.0f);
  for (std::vector<float>& fills : bottle_fill_) std::fill(fills.begin(), fills.end(), 0.0f);
}

void BoardView::OnEvents(const std::vector<Event>& events, const Board& board) {
  auto add = [&](Marker::Kind kind, int node, int slot = -1) {
    for (const Marker& marker : markers_) {
      if (marker.kind == kind && marker.node == node && marker.slot == slot && kind != Marker::NearMiss) return;
    }
    markers_.push_back({kind, node, 0.0f, slot});
  };
  for (const Event& e : events) {
    switch (e.type) {
      case EventType::Enter: {
        // A turnstile turns as each drop goes through, unless it sits idle (on a fork or a pipe).
        const int piece = e.node >= 0 ? board.PieceAt(e.node) : -1;
        if (piece >= 0 && board.PieceInfo(piece).piece.type == PieceType::Turnstile &&
            board.RoleOf(piece) == PieceRole::Acts) {
          anims_[static_cast<size_t>(e.node)].spin_target += kPi * 0.5f;
        }
        break;
      }
      case EventType::Leave:
        if (level_->nodes[static_cast<size_t>(e.node)].kind == NodeKind::Mill)
          anims_[static_cast<size_t>(e.node)].spin_target += kPi / 3.0f;  // a notch
        break;
      case EventType::Mix: anims_[static_cast<size_t>(e.node)].flash = 1.0f; break;
      case EventType::Flip: anims_[static_cast<size_t>(e.node)].flash = 1.0f; break;
      case EventType::Collide: add(Marker::Mud, e.node); break;
      case EventType::NearMiss: add(Marker::NearMiss, e.node); break;
      case EventType::Land: {
        const Node& node = level_->nodes[static_cast<size_t>(e.node)];
        const Color want =
            node.kind == NodeKind::Rack && e.slot >= 0 ? node.slots[static_cast<size_t>(e.slot)].want : node.want;
        if ((node.kind == NodeKind::Cup || node.kind == NodeKind::Rack) &&
            !(e.bead.a == want && (!e.bead.IsBlend() || e.bead.b == want)) && !e.bead.IsMud()) {
          add(Marker::WrongCup, e.node, e.slot);
        }
        break;
      }
      case EventType::Spill: add(Marker::Spill, e.node, e.slot); break;
      case EventType::Finished:
        for (size_t n = 0; n < level_->nodes.size(); ++n) {
          const Node& node = level_->nodes[n];
          if (node.kind == NodeKind::Cup && board.Cup(static_cast<int>(n)).count < node.count)
            add(Marker::Dry, static_cast<int>(n));
          if (node.kind != NodeKind::Rack) continue;
          for (size_t s = 0; s < node.slots.size(); ++s) {
            if (board.Bottle(static_cast<int>(n), static_cast<int>(s)).count < node.slots[s].count)
              add(Marker::Dry, static_cast<int>(n), static_cast<int>(s));
          }
        }
        break;
      case EventType::Jammed: add(Marker::Jam, -1); break;
      default: break;
    }
  }
}

void BoardView::Update(float dt, const Board& board) {
  if (!level_) return;
  for (size_t n = 0; n < anims_.size(); ++n) {
    NodeAnim& anim = anims_[n];
    const Node& node = level_->nodes[n];
    if (node.kind == NodeKind::Rack) {
      // The turntable steps round with the board's clock (counted, so it never spins back at a wrap).
      anim.spin_target = board.Started() ? static_cast<float>(board.Ticks() / std::max(1, node.period)) : 0.0f;
      if (!board.Started()) anim.spin = 0.0f;
    }
    anim.spin += (anim.spin_target - anim.spin) * std::min(1.0f, dt * 12.0f);
    anim.flash = std::max(0.0f, anim.flash - dt * 2.0f);
    const int piece = board.PieceAt(static_cast<int>(n));
    if (piece >= 0 && board.PieceInfo(piece).piece.type == PieceType::Flipper) {
      const float target =
          static_cast<float>(board.Started() ? board.FlipperNext(static_cast<int>(n)) : board.Setting(piece));
      anim.arm += (target - anim.arm) * std::min(1.0f, dt * 14.0f);
    }
    if (piece >= 0 && board.PieceInfo(piece).piece.type == PieceType::Mixer && board.Started() &&
        board.InsideCount(static_cast<int>(n)) > 0) {
      anim.spin += dt * 5.0f;
      anim.spin_target = anim.spin;
    }
    if (node.kind == NodeKind::Cup) {
      const float target = board.Started() ? static_cast<float>(board.Cup(static_cast<int>(n)).count) : 0.0f;
      cup_fill_[n] += (target - cup_fill_[n]) * std::min(1.0f, dt * 8.0f);
    }
    for (size_t s = 0; s < bottle_fill_[n].size(); ++s) {
      const float target =
          board.Started() ? static_cast<float>(board.Bottle(static_cast<int>(n), static_cast<int>(s)).count) : 0.0f;
      bottle_fill_[n][s] += (target - bottle_fill_[n][s]) * std::min(1.0f, dt * 8.0f);
    }
  }
  for (Marker& marker : markers_) marker.age += dt;
  std::erase_if(markers_, [](const Marker& marker) { return marker.kind == Marker::NearMiss && marker.age > 1.4f; });
}

void BoardView::DrawPipes(Canvas& canvas) const {
  if (!level_) return;
  const float s = layout_.scale;
  std::vector<std::vector<glm::vec2>> lines;
  for (const Pipe& pipe : level_->pipes) {
    std::vector<glm::vec2> points;
    // From node centre to node centre, so pipes reach into junctions and sockets.
    points.push_back(layout_.ToCanvas(level_->nodes[static_cast<size_t>(pipe.from)].at));
    for (Vec2 p : pipe.via) points.push_back(layout_.ToCanvas(p));
    points.push_back(layout_.ToCanvas(level_->nodes[static_cast<size_t>(pipe.to)].at));
    lines.push_back(std::move(points));
  }
  for (const auto& line : lines) Polyline(canvas, line, 24.0f * s, ui_color::kPipeEdge);
  auto junction = [](NodeKind kind) {
    return kind == NodeKind::Cross || kind == NodeKind::Merge || kind == NodeKind::Mill;
  };
  for (const Node& node : level_->nodes) {
    const glm::vec2 c = layout_.ToCanvas(node.at);
    if (junction(node.kind)) canvas.Circle(c, 19.0f * s, ui_color::kPipeEdge);
    if (node.kind == NodeKind::Spout) canvas.Circle(c, 26.0f * s, ui_color::kPipeEdge);
    if (node.kind == NodeKind::Drain) canvas.Circle(c, 17.0f * s, ui_color::kPipeEdge);
  }
  for (const auto& line : lines) Polyline(canvas, line, 17.0f * s, ui_color::kPipeInside);
  for (const Node& node : level_->nodes) {
    const glm::vec2 c = layout_.ToCanvas(node.at);
    if (junction(node.kind)) canvas.Circle(c, 15.0f * s, ui_color::kPipeInside);
    if (node.kind == NodeKind::Spout) canvas.Circle(c, 22.0f * s, ui_color::kPipeInside);
    if (node.kind == NodeKind::Drain) canvas.Circle(c, 14.0f * s, ui_color::kPipeInside);
    if (node.kind == NodeKind::Cup) {
      const glm::vec2 min = layout_.ToCanvas({node.at.x - kCupHalf, node.at.y});
      const glm::vec2 max = layout_.ToCanvas({node.at.x + kCupHalf, node.at.y + kCupHeight});
      canvas.FillRect({min, max}, WithAlpha(ui_color::kPipeInside, 0.9f), 6.0f * s);
    }
  }
  // Faint tick marks, one per cell: what you see is the timing.
  const Rgba tick = WithAlpha(ui_color::kInk, 0.07f);
  for (const Pipe& pipe : level_->pipes) {
    for (int k = 1; k < pipe.cells; ++k) {
      const float d = pipe.length * static_cast<float>(k) / static_cast<float>(pipe.cells);
      const glm::vec2 p = layout_.ToCanvas(PointAlong(pipe.points, d));
      const glm::vec2 q = layout_.ToCanvas(PointAlong(pipe.points, d + 0.5f));
      const glm::vec2 n = Perp(Normal(q - p));
      canvas.Line(p + n * (6.0f * s), p + n * (8.0f * s), 1.2f * s, tick);
    }
  }
}

void BoardView::DrawLiquid(Liquid& liquid, const Board& board, float alpha, const Palette& palette, float time) const {
  if (!level_) return;
  const float s = layout_.scale;
  if (board.Started()) {
    for (const Drop& drop : board.Drops()) {
      if (!drop.alive) continue;
      const glm::vec2 now = V(DropPoint(*level_, drop, drop.loc));
      const glm::vec2 before = drop.prev.kind == Loc::None ? now : V(DropPoint(*level_, drop, drop.prev));
      glm::vec2 along = now - before;
      if (glm::length(along) < 1e-3f && drop.loc.kind == Loc::OnPipe) {
        const Pipe& pipe = level_->pipes[static_cast<size_t>(drop.loc.index)];
        const float d = (static_cast<float>(drop.loc.cell) + 0.5f) * pipe.length / static_cast<float>(pipe.cells);
        along = V(PointAlong(pipe.points, d + 1.0f)) - V(PointAlong(pipe.points, d));
      }
      const glm::vec2 at = before + (now - before) * std::clamp(alpha, 0.0f, 1.0f);
      const Rgba front = LiquidColor(palette, drop.bead.a);
      const Rgba back = drop.bead.IsBlend() ? LiquidColor(palette, drop.bead.b) : front;
      liquid.Blob(layout_.ToCanvas({at.x, at.y}), Normal(along), 12.0f * s, front, back,
                  static_cast<uint32_t>(drop.id) + 1u);
    }
  }
  for (size_t n = 0; n < level_->nodes.size(); ++n) {
    const Node& node = level_->nodes[n];
    if (node.kind == NodeKind::Spout) {
      // A slow swirl of what's still to pour.
      const int left = static_cast<int>(node.beads.size()) - (board.Started() ? board.Poured(static_cast<int>(n)) : 0);
      if (left <= 0) continue;
      const threadlock::Bead& next = node.beads[node.beads.size() - static_cast<size_t>(left)];
      const glm::vec2 c = layout_.ToCanvas(node.at);
      for (int k = 0; k < 10; ++k) {
        const bool inner = k % 2 != 0;
        const float a = static_cast<float>(k / 2) / 5.0f * 2.0f * kPi + time * (inner ? -1.6f : 1.1f);
        const float r = (inner ? 7.0f : 12.0f) * s;
        const Rgba color = LiquidColor(palette, (inner && next.IsBlend()) ? next.b : next.a);
        liquid.Blob(c + glm::vec2(std::cos(a), std::sin(a)) * r, {-std::sin(a), std::cos(a)}, 7.0f * s, color, color,
                    static_cast<uint32_t>(9000 + n * 16 + static_cast<size_t>(k)));
      }
    }
    if (node.kind == NodeKind::Cup && cup_fill_[n] > 0.01f) {
      const CupState& cup = board.Started() ? board.Cup(static_cast<int>(n)) : CupState{};
      const Rgba color = cup.impure ? kMud : LiquidColor(palette, node.want);
      const glm::vec2 min = layout_.ToCanvas({node.at.x - kCupHalf + 3.0f, node.at.y + 6.0f});
      const glm::vec2 max = layout_.ToCanvas({node.at.x + kCupHalf - 3.0f, node.at.y + kCupHeight - 3.0f});
      liquid.Pool(min, max, cup_fill_[n] / static_cast<float>(std::max(1, node.count)), color,
                  static_cast<uint32_t>(n) * 7919u);
    }
  }
}

Vec2 BoardView::BottleAt(int rack, int slot) const {
  const Node& node = level_->nodes[static_cast<size_t>(rack)];
  const int k = std::max(1, static_cast<int>(node.slots.size()));
  const float radius = k <= 2 ? 22.0f : 30.0f;
  // The bottle under the pipe sits at the top, right at `at`; the table turns one bottle per step.
  const float steps = anims_[static_cast<size_t>(rack)].spin;
  const float a = -kPi * 0.5f + 2.0f * kPi * (static_cast<float>(slot - node.first) - steps) / static_cast<float>(k);
  return {node.at.x + std::cos(a) * radius, node.at.y + radius + std::sin(a) * radius};
}

glm::vec2 BoardView::MarkerCanvas(int node, int slot) const {
  if (slot >= 0 && level_->nodes[static_cast<size_t>(node)].kind == NodeKind::Rack)
    return layout_.ToCanvas(BottleAt(node, slot));
  return NodeCanvas(node);
}

glm::vec2 BoardView::FlapHandle(const Board& board, int fork) const {
  const Node& node = level_->nodes[static_cast<size_t>(fork)];
  const glm::vec2 d = OutDir(*level_, node, static_cast<size_t>(std::max(0, board.Flap(fork))));
  return NodeCanvas(fork) + d * ((board.PieceAt(fork) >= 0 ? 37.0f : 13.0f) * layout_.scale);
}

glm::vec2 BoardView::PieceCenter(const Board& board, int node) const {
  const Node& n = level_->nodes[static_cast<size_t>(node)];
  const int piece = board.PieceAt(node);
  if (piece >= 0 && board.PieceInfo(piece).piece.type == PieceType::TwinGate && n.twin >= 0) {
    return (NodeCanvas(node) + NodeCanvas(n.twin)) * 0.5f;
  }
  return NodeCanvas(node);
}

void BoardView::DrawParts(Canvas& canvas, const Board& board, const Palette& palette, const wlw::ui::Font& font,
                          const Options& options) const {
  if (!level_) return;
  const float s = layout_.scale;
  const Rgba ink = ui_color::kInk;
  for (size_t n = 0; n < level_->nodes.size(); ++n) {
    const Node& node = level_->nodes[n];
    const glm::vec2 c = layout_.ToCanvas(node.at);
    switch (node.kind) {
      case NodeKind::Spout: {
        canvas.Ring(c, 22.0f * s, 2.5f * s, WithAlpha(ink, 0.55f));
        if (!node.beads.empty()) Glyph(canvas, node.beads[0].a, c, 4.0f * s, WithAlpha(ink, 0.8f));
        // Its beads still to pour, in order: above it when there's room, else beside it (towards the
        // board's middle), in rows of six.
        const int poured = board.Started() ? board.Poured(static_cast<int>(n)) : 0;
        const int left = static_cast<int>(node.beads.size()) - poured;
        const bool above = node.at.y >= 64.0f;
        const int per_row = above ? 8 : 6;
        const float step = above ? 10.0f : 7.5f;
        const float side = node.at.x > 220.0f ? -1.0f : 1.0f;
        for (int k = 0; k < left; ++k) {
          const int row = k / per_row, col = k % per_row;
          Vec2 at;
          if (above) {
            const int in_row = std::min(per_row, left - row * per_row);
            at = {node.at.x + (static_cast<float>(col) - static_cast<float>(in_row - 1) * 0.5f) * step,
                  node.at.y - 32.0f - static_cast<float>(row) * step};
          } else {
            at = {node.at.x + side * (30.0f + static_cast<float>(col) * step),
                  node.at.y - 4.0f + static_cast<float>(row) * step};
          }
          DrawBead(canvas, node.beads[static_cast<size_t>(poured + k)], layout_.ToCanvas(at), (above ? 4.0f : 3.2f) * s,
                   palette, 1.0f);
        }
        break;
      }
      case NodeKind::Cup: {
        const CupState& cup = board.Started() ? board.Cup(static_cast<int>(n)) : CupState{};
        const Rgba color = cup.impure ? kMud : LiquidColor(palette, node.want);
        const glm::vec2 tl = layout_.ToCanvas({node.at.x - kCupHalf, node.at.y});
        const glm::vec2 br = layout_.ToCanvas({node.at.x + kCupHalf, node.at.y + kCupHeight});
        canvas.Line(tl, {tl.x, br.y}, 3.0f * s, color);
        canvas.Line({tl.x, br.y}, br, 3.0f * s, color);
        canvas.Line(br, {br.x, tl.y}, 3.0f * s, color);
        for (int k = 1; k < node.count; ++k) {
          const float y = node.at.y + kCupHeight - 3.0f -
                          (kCupHeight - 9.0f) * static_cast<float>(k) / static_cast<float>(node.count);
          canvas.Line(layout_.ToCanvas({node.at.x + kCupHalf - 8.0f, y}),
                      layout_.ToCanvas({node.at.x + kCupHalf - 3.0f, y}), 1.4f * s, WithAlpha(ink, 0.3f));
        }
        char text[24];
        std::snprintf(text, sizeof(text), "%d/%d", cup.count, node.count);
        const glm::vec2 label = layout_.ToCanvas({node.at.x + 4.0f, node.at.y + kCupHeight + 11.0f});
        canvas.Text(font, text, label, 15.0f * s, Align::Center,
                    TextStyle::Outlined(cup.impure || cup.spilled ? ui_color::kBad : ink, ui_color::kBackground));
        Glyph(canvas, node.want, label + glm::vec2(-20.0f * s, 0.0f), 4.0f * s, LiquidColor(palette, node.want));
        break;
      }
      case NodeKind::Drain: {
        canvas.Ring(c, 14.0f * s, 2.5f * s, ui_color::kFaint);
        for (float dy : {-6.0f, 0.0f, 6.0f}) {
          canvas.Line(c + glm::vec2(-8.0f, dy) * s, c + glm::vec2(8.0f, dy) * s, 2.0f * s, ui_color::kFaint);
        }
        break;
      }
      case NodeKind::Rack: {
        // A turntable of bottles seen from above; the one under the pipe is at the top. Each bottle is
        // a cup of its own.
        const int k = static_cast<int>(node.slots.size());
        const float radius = k <= 2 ? 22.0f : 30.0f;
        const glm::vec2 centre = layout_.ToCanvas({node.at.x, node.at.y + radius});
        canvas.Circle(centre, (radius + 17.0f) * s, WithAlpha(ui_color::kPanel, 0.75f));
        canvas.Ring(centre, (radius + 17.0f) * s, 2.0f * s, WithAlpha(ink, 0.3f));
        canvas.Circle(centre, 4.0f * s, WithAlpha(ink, 0.5f));
        for (int slot = 0; slot < k; ++slot) {
          const Slot& want = node.slots[static_cast<size_t>(slot)];
          const CupState& bottle = board.Started() ? board.Bottle(static_cast<int>(n), slot) : CupState{};
          const glm::vec2 b = layout_.ToCanvas(BottleAt(static_cast<int>(n), slot));
          const Rgba color = bottle.impure ? kMud : LiquidColor(palette, want.want);
          const float fill = std::clamp(
              bottle_fill_[n][static_cast<size_t>(slot)] / static_cast<float>(std::max(1, want.count)), 0.0f, 1.0f);
          canvas.Circle(b, 12.5f * s, WithAlpha(ui_color::kPipeInside, 0.95f));
          if (fill > 0.01f) canvas.Circle(b, 11.0f * s * std::sqrt(fill), WithAlpha(color, 0.85f));
          canvas.Ring(b, 12.5f * s, 2.5f * s, color);
          char text[24];
          std::snprintf(text, sizeof(text), "%d/%d", bottle.count, want.count);
          canvas.Text(
              font, text, b + glm::vec2(0.0f, 4.5f * s), 12.0f * s, Align::Center,
              TextStyle::Outlined(bottle.impure || bottle.spilled ? ui_color::kBad : ink, ui_color::kBackground));
        }
        break;
      }
      case NodeKind::Mill: {
        // A wheel that turns a notch per drop; dashed gears run to the gates it flips.
        const NodeAnim& anim = anims_[n];
        for (int gate : node.gears)
          DashedLine(canvas, c, NodeCanvas(gate), 1.5f * s, WithAlpha(ui_color::kStar, 0.3f + 0.5f * anim.flash), 12);
        canvas.Circle(c, 16.0f * s, WithAlpha(ui_color::kPanel, 0.8f));
        canvas.Ring(c, 16.0f * s, 2.5f * s, ink);
        for (int spoke = 0; spoke < 6; ++spoke) {
          const float a = anim.spin + static_cast<float>(spoke) * kPi / 3.0f;
          canvas.Line(c, c + glm::vec2(std::cos(a), std::sin(a)) * (15.0f * s), 2.5f * s, ink);
        }
        canvas.Circle(c, 4.0f * s, ink);
        break;
      }
      case NodeKind::Gate: {
        // Open: two posts beside the pipe and a green lamp. Shut: a bar across it and a red lamp.
        const bool open = board.GateOpen(static_cast<int>(n));
        const glm::vec2 d = node.ins.empty() ? glm::vec2(0.0f, 1.0f) : InDir(*level_, node, 0);
        const glm::vec2 p = Perp(d);
        if (open) {
          for (float side : {-1.0f, 1.0f}) {
            canvas.Line(c + p * (15.0f * side * s) - d * (5.0f * s), c + p * (15.0f * side * s) + d * (5.0f * s),
                        4.0f * s, ink);
          }
        } else {
          canvas.Line(c - p * (16.0f * s), c + p * (16.0f * s), 5.5f * s, ink);
        }
        const glm::vec2 lamp = c + p * (23.0f * s) - d * (8.0f * s);
        const Rgba lamp_color = open ? ui_color::kGood : ui_color::kBad;
        canvas.Circle(lamp, (4.5f + 3.0f * anims_[n].flash) * s, lamp_color);
        canvas.Ring(lamp, 4.5f * s, 1.5f * s, ui_color::kBackground);
        break;
      }
      default: break;
    }
  }

  // A fork with no router on it shows its flap (where everything goes). A piece that leaves the flap in
  // charge (a tank, a turnstile...) is drawn over it, so the flap is drawn again outside it below.
  auto draw_flap = [&](int n, bool covered) {
    const Node& node = level_->nodes[static_cast<size_t>(n)];
    const glm::vec2 c = layout_.ToCanvas(node.at);
    const glm::vec2 d = OutDir(*level_, node, static_cast<size_t>(std::max(0, board.Flap(n))));
    const glm::vec2 handle = FlapHandle(board, n);
    canvas.Line(covered ? c + d * (25.0f * s) : c, handle - d * (1.0f * s), 3.0f * s, WithAlpha(ink, 0.9f));
    if (!covered) canvas.Circle(c, 3.0f * s, ink);
    if (node.flap_tappable) {
      canvas.Circle(handle, 4.5f * s, ui_color::kGood);
      canvas.Ring(handle, 4.5f * s, 1.5f * s, ui_color::kBackground);
    }
  };
  for (size_t n = 0; n < level_->nodes.size(); ++n) {
    if (board.FlapInCharge(static_cast<int>(n)) && board.PieceAt(static_cast<int>(n)) < 0)
      draw_flap(static_cast<int>(n), false);
  }

  // Free sockets all look the same (any piece goes in any socket); while a piece is dragged they all
  // light up. A twin pair is drawn as one, since a twin gate takes both halves.
  for (size_t n = 0; n < level_->nodes.size(); ++n) {
    const Node& node = level_->nodes[n];
    if (!node.socket || board.PieceAt(static_cast<int>(n)) >= 0) continue;
    if (node.twin >= 0 && node.twin < static_cast<int>(n) && board.PieceAt(node.twin) < 0) continue;  // with its pair
    const glm::vec2 c = layout_.ToCanvas(node.at);
    const bool dragging = options.drag_type >= 0;
    const bool target = options.drag_target == static_cast<int>(n) ||
                        (node.twin >= 0 && options.drag_target == node.twin && board.PieceAt(node.twin) < 0);
    float a = options.editing ? 0.32f + 0.12f * std::sin(options.time * 3.0f) : 0.2f;
    float width = 2.0f * s;
    if (dragging) a = 0.9f;
    if (target) {
      a = 1.0f;
      width = 3.2f * s;
    }
    const Rgba color = WithAlpha(dragging ? ui_color::kGood : ink, a);
    if (target) canvas.Circle(c, 24.0f * s, WithAlpha(ui_color::kGood, 0.16f));
    for (int dash = 0; dash < 12; ++dash)
      canvas.Arc(c, 19.0f * s, width, static_cast<float>(dash) * kPi / 6.0f, 0.3f, color);
    if (node.twin >= 0 && board.PieceAt(node.twin) < 0) {
      const glm::vec2 other = NodeCanvas(node.twin);
      for (int dash = 0; dash < 12; ++dash)
        canvas.Arc(other, 19.0f * s, width, static_cast<float>(dash) * kPi / 6.0f, 0.3f, color);
      const glm::vec2 d = Normal(other - c);
      DashedLine(canvas, c + d * (21.0f * s), other - d * (21.0f * s), width, color, 4);
    }
  }

  // The solution's ghosts (Show the solution).
  if (options.ghosts && options.ghost_placement) {
    const Placement& ghost = *options.ghost_placement;
    for (size_t i = 0; i < ghost.at.size() && i < level_->tray.size(); ++i) {
      if (ghost.at[i] < 0 || level_->tray[i].locked) continue;
      const int node = ghost.at[i];
      if (board.PieceAt(node) >= 0 && board.PieceInfo(board.PieceAt(node)).piece == level_->tray[i].piece &&
          (!HasSetting(level_->tray[i].piece.type) || board.Setting(board.PieceAt(node)) == ghost.setting[i])) {
        continue;  // already there
      }
      PieceLook look;
      look.setting = ghost.setting[i];
      look.alpha = 0.32f + 0.1f * std::sin(options.time * 4.0f);
      DrawPiece(canvas, level_->tray[i].piece, NodeCanvas(node), s, look, palette, level_,
                &level_->nodes[static_cast<size_t>(node)], &font);
    }
  }

  // Placed pieces. During a run a piece with nothing to do there sits dim (the replay shows why).
  for (int i = 0; i < board.PieceCount(); ++i) {
    const int node = board.PieceNode(i);
    if (node < 0) continue;
    const NodeAnim& anim = anims_[static_cast<size_t>(node)];
    const Piece& piece = board.PieceInfo(i).piece;
    PieceLook look;
    look.setting = board.Setting(i);
    look.spin = anim.spin;
    look.arm = piece.type == PieceType::Flipper ? anim.arm : -1.0f;
    look.locked = board.Locked(i);
    if (board.Started() && board.RoleOf(i) == PieceRole::Idle) look.alpha = 0.38f;
    if (piece.type == PieceType::Overflow && board.Started())
      look.remaining = std::max(0, piece.n - board.OverflowPassed(node));
    DrawPiece(canvas, piece, PieceCenter(board, node), s, look, palette, level_,
              &level_->nodes[static_cast<size_t>(node)], &font);
  }
  for (size_t n = 0; n < level_->nodes.size(); ++n) {
    if (board.FlapInCharge(static_cast<int>(n)) && board.PieceAt(static_cast<int>(n)) >= 0)
      draw_flap(static_cast<int>(n), true);
  }

  if (options.hint_node >= 0) {
    const float pulse = 0.5f + 0.5f * std::sin(options.time * 6.0f);
    canvas.Ring(NodeCanvas(options.hint_node), (28.0f + 4.0f * pulse) * s, 3.0f * s, WithAlpha(ui_color::kStar, 0.9f));
  }
}

void BoardView::DrawMarkers(Canvas& canvas, const Board& board, const wlw::ui::Font& font, float time) const {
  if (!level_) return;
  const float s = layout_.scale;
  for (const Marker& marker : markers_) {
    const float pop = std::min(1.0f, marker.age * 6.0f);
    switch (marker.kind) {
      case Marker::Mud: {
        const glm::vec2 c = NodeCanvas(marker.node);
        canvas.Circle(c, 13.0f * s * pop, kMud);
        for (int i = 0; i < 6; ++i) {
          const float a = kPi / 3.0f * static_cast<float>(i) + 0.35f;
          const float d = (21.0f + static_cast<float>(i % 2) * 5.0f) * s * pop;
          canvas.Circle(c + glm::vec2(std::cos(a), std::sin(a)) * d, (2.5f + static_cast<float>(i % 3)) * s, kMud);
        }
        canvas.Text(font, "mud", c + glm::vec2(0.0f, 36.0f * s), 17.0f * s, Align::Center,
                    TextStyle::Outlined(ui_color::kBad, ui_color::kBackground));
        break;
      }
      case Marker::NearMiss: {
        const glm::vec2 c = NodeCanvas(marker.node) + glm::vec2(22.0f, -22.0f) * s;
        const float fade = std::clamp(1.4f - marker.age, 0.0f, 1.0f);
        Sparkle(canvas, c, (8.0f + 6.0f * pop) * s, WithAlpha(ui_color::kGood, fade));
        canvas.Text(font, "near miss", c + glm::vec2(0.0f, -16.0f * s), 13.0f * s, Align::Center,
                    TextStyle::Outlined(ui_color::kGood, ui_color::kBackground, fade));
        break;
      }
      case Marker::WrongCup:
      case Marker::Dry:
      case Marker::Spill: {
        const Node& node = level_->nodes[static_cast<size_t>(marker.node)];
        const char* text = marker.kind == Marker::WrongCup ? "wrong colour"
                           : marker.kind == Marker::Dry    ? "dry"
                                                           : "spill";
        if (node.kind == NodeKind::Rack) {  // over the bottle, wherever the table has turned it
          canvas.Text(font, text, MarkerCanvas(marker.node, marker.slot) + glm::vec2(0.0f, -17.0f * s), 14.0f * s,
                      Align::Center, TextStyle::Outlined(ui_color::kBad, ui_color::kBackground));
          break;
        }
        const glm::vec2 top = layout_.ToCanvas({node.at.x, node.at.y - 10.0f});
        canvas.Text(font, text, top, 15.0f * s, Align::Center,
                    TextStyle::Outlined(ui_color::kBad, ui_color::kBackground));
        if (marker.kind == Marker::Spill) {
          for (int k = 0; k < 3; ++k) {
            const float y = std::fmod(time * 40.0f + static_cast<float>(k) * 20.0f, 60.0f);
            canvas.Circle(layout_.ToCanvas({node.at.x + kCupHalf + 5.0f, node.at.y + 4.0f + y}), 3.0f * s, kMud);
          }
        }
        break;
      }
      case Marker::Jam: {
        const JamReport jam = board.Jam();
        for (int node : jam.stuck_nodes) {
          const NodeKind kind = level_->nodes[static_cast<size_t>(node)].kind;
          if (kind == NodeKind::Cup || kind == NodeKind::Drain || kind == NodeKind::Spout) continue;
          Padlock(canvas, NodeCanvas(node) + glm::vec2(24.0f, -22.0f) * s, 1.1f * s * pop, ui_color::kBad);
        }
        // The circle of waiting: arrows from each node to the next.
        for (size_t i = 0; i < jam.cycle.size() && jam.cycle.size() >= 2; ++i) {
          const glm::vec2 a = NodeCanvas(jam.cycle[i]), b = NodeCanvas(jam.cycle[(i + 1) % jam.cycle.size()]);
          const glm::vec2 mid = (a + b) * 0.5f + Perp(Normal(b - a)) * (40.0f * s);
          const float dash = std::fmod(time * 2.0f, 1.0f);
          for (int k = 0; k < 8; ++k) {
            const float t0 = (static_cast<float>(k) + dash) / 8.0f, t1 = t0 + 0.06f;
            auto at = [&](float t) { return (1 - t) * (1 - t) * a + 2 * (1 - t) * t * mid + t * t * b; };
            if (t1 <= 1.0f) canvas.Line(at(t0), at(t1), 2.5f * s, ui_color::kBad);
          }
          const glm::vec2 tip = (0.18f * 0.18f) * a + 2 * 0.82f * 0.18f * mid + 0.82f * 0.82f * b;
          const glm::vec2 d = Normal(b - mid);
          canvas.Triangle(tip + d * (7.0f * s), tip - d * (4.0f * s) + Perp(d) * (6.0f * s),
                          tip - d * (4.0f * s) - Perp(d) * (6.0f * s), ui_color::kBad);
        }
        if (jam.lonely >= 0) {
          const bool gate = level_->nodes[static_cast<size_t>(jam.lonely)].kind == NodeKind::Gate;
          canvas.Text(font, gate ? "never opens" : "waits forever", NodeCanvas(jam.lonely) + glm::vec2(0.0f, 34.0f * s),
                      14.0f * s, Align::Center, TextStyle::Outlined(ui_color::kBad, ui_color::kBackground));
        }
        break;
      }
    }
  }
}

void BoardView::DrawSpotlight(Canvas& canvas, int node, float amount) const {
  if (!level_ || node < 0 || amount <= 0.0f) return;
  const glm::vec2 c = NodeCanvas(node);
  const float r = 70.0f * layout_.scale;
  canvas.Arc(c, r + 1500.0f, 3000.0f, 0.0f, 7.0f, WithAlpha(ui_color::kBackground, 0.62f * amount));
  canvas.Ring(c, r, 2.0f, WithAlpha(ui_color::kInk, 0.35f * amount));
}

// ---- Hit tests ----

int BoardView::SocketNear(glm::vec2 point, float radius) const {
  if (!level_) return -1;
  int best = -1;
  float best_distance = radius;
  for (size_t n = 0; n < level_->nodes.size(); ++n) {
    const Node& node = level_->nodes[n];
    if (!node.socket) continue;
    float d = glm::distance(point, NodeCanvas(static_cast<int>(n)));
    if (node.twin >= 0)
      d = std::min(d, glm::distance(point, (NodeCanvas(static_cast<int>(n)) + NodeCanvas(node.twin)) * 0.5f));
    if (d < best_distance) {
      best_distance = d;
      best = static_cast<int>(n);
    }
  }
  return best;
}

int BoardView::PieceNear(const Board& board, glm::vec2 point, float radius) const {
  int best = -1;
  float best_distance = radius;
  for (int i = 0; i < board.PieceCount(); ++i) {
    const int node = board.PieceNode(i);
    if (node < 0) continue;
    const float d = glm::distance(point, PieceCenter(board, node));
    if (d < best_distance) {
      best_distance = d;
      best = i;
    }
  }
  return best;
}

int BoardView::FlapNear(const Board& board, glm::vec2 point, float radius) const {
  if (!level_) return -1;
  int best = -1;
  float best_distance = radius;
  for (size_t n = 0; n < level_->nodes.size(); ++n) {
    if (!board.FlapTappable(static_cast<int>(n))) continue;
    const float d = std::min(
        glm::distance(point, FlapHandle(board, static_cast<int>(n))),
        board.PieceAt(static_cast<int>(n)) < 0 ? glm::distance(point, NodeCanvas(static_cast<int>(n))) : radius);
    if (d < best_distance) {
      best_distance = d;
      best = static_cast<int>(n);
    }
  }
  return best;
}

}  // namespace threadlock
