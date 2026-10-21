#include "board_view.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "art_shapes.h"
#include "workshop_art.h"

namespace threadlock {
namespace {

using art::kPi;
using wlw::ui::Align;
using wlw::ui::Canvas;
using wlw::ui::TextStyle;
namespace brass = art::brass;

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

// An arrow head at `tip` pointing along `d`.
void Arrow(Canvas& canvas, glm::vec2 tip, glm::vec2 d, float s, const Rgba& color) {
  canvas.Triangle(tip, tip - d * (8.0f * s) + Perp(d) * (6.0f * s), tip - d * (8.0f * s) - Perp(d) * (6.0f * s), color);
}

}  // namespace

// ---- Pieces: the brass toolkit ----

void DrawPiece(Canvas& canvas, const Piece& piece, glm::vec2 c, float s, const PieceLook& look, const Palette& palette,
               const Level* level, const Node* node, const wlw::ui::Font* font) {
  const float alpha = look.alpha;
  const Rgba mark = WithAlpha(brass::kIndicator, alpha);
  const Rgba accent = WithAlpha(look.accent, alpha);
  const bool on_board = level && node;
  auto out_dir = [&](int side) {
    return on_board ? OutDir(*level, *node, static_cast<size_t>(side))
                    : (side == 0 ? glm::vec2(-0.7f, 0.7f) : glm::vec2(0.7f, 0.7f));
  };
  auto in_dir = [&](int lane) {
    if (on_board) return InDir(*level, *node, static_cast<size_t>(lane));
    return lane == 0 ? glm::vec2(0.0f, 1.0f) : glm::vec2(-1.0f, 0.0f);
  };
  auto liquid = [&](Color color) { return WithAlpha(LiquidColor(palette, color), alpha); };
  switch (piece.type) {
    case PieceType::Separator: {
      art::Medallion(canvas, c, 22.0f * s, alpha);
      const int side_a = look.setting == 0 ? 0 : 1;
      const glm::vec2 in = on_board ? InDir(*level, *node, 0) : glm::vec2(0.0f, 1.0f);
      canvas.Line(c - in * (11.0f * s), c, 3.4f * s, mark);
      canvas.Line(c, c + out_dir(side_a) * (11.5f * s), 4.4f * s, liquid(piece.a));
      canvas.Line(c, c + out_dir(1 - side_a) * (11.5f * s), 4.4f * s, liquid(piece.b));
      canvas.Circle(c, 3.0f * s, mark);
      break;
    }
    case PieceType::Turnstile: {
      art::Medallion(canvas, c, 22.0f * s, alpha);
      for (int arm = 0; arm < 4; ++arm) {
        const float a = look.spin + kPi * (0.25f + 0.5f * static_cast<float>(arm));
        canvas.Line(c, c + glm::vec2(std::cos(a), std::sin(a)) * (11.5f * s), 3.4f * s, mark);
      }
      canvas.Circle(c, 4.2f * s, mark);
      // The arrow: which lane goes first when both arrive together (it points the way that lane flows).
      const glm::vec2 d = in_dir(look.setting);
      Arrow(canvas, c - d * (26.0f * s), d, s, accent);
      break;
    }
    case PieceType::TankShort:
    case PieceType::TankTall: {
      // A glass cylinder between brass caps: you see the drop wait inside.
      const float h = (piece.type == PieceType::TankShort ? 32.0f : 46.0f) * s, w = 26.0f * s;
      const glm::vec2 min = c - glm::vec2(w, h) * 0.5f, max = c + glm::vec2(w, h) * 0.5f;
      canvas.FillRect({min + glm::vec2(1.0f * s, 4.0f * s), max + glm::vec2(1.0f * s, 6.0f * s)},
                      Rgba{0.0f, 0.0f, 0.0f, 0.3f * alpha}, 6.0f * s);
      canvas.FillRect({min, max}, WithAlpha(Hex(0x0A1626), 0.5f * alpha), 6.0f * s);
      canvas.FillRect({min + glm::vec2(2.0f * s, 0.0f), {min.x + 5.0f * s, max.y}},
                      WithAlpha(Hex(0xCFE6FF), 0.12f * alpha), 1.5f * s);
      for (float x : {min.x + 1.0f * s, max.x - 1.0f * s})
        canvas.Line({x, min.y + 2.0f * s}, {x, max.y - 2.0f * s}, 1.6f * s, WithAlpha(Hex(0xB9C8DC), alpha));
      for (int i = 1; i < 4; ++i) {
        const float y = min.y + h * static_cast<float>(i) / 4.0f;
        canvas.Line({max.x - 8.0f * s, y}, {max.x - 3.5f * s, y}, 1.2f * s, WithAlpha(brass::kLight, 0.5f * alpha));
      }
      art::Plate(canvas, {min + glm::vec2(-3.0f * s, -5.0f * s), {max.x + 3.0f * s, min.y + 3.0f * s}}, 2.5f * s, 0.0f,
                 alpha);
      art::Plate(canvas, {{min.x - 3.0f * s, max.y - 3.0f * s}, max + glm::vec2(3.0f * s, 5.0f * s)}, 2.5f * s, 0.0f,
                 alpha);
      if (font) {
        canvas.Text(*font, piece.type == PieceType::TankShort ? "+4" : "+8", c + glm::vec2(0.0f, -h * 0.5f - 13.0f * s),
                    13.0f * s, Align::Center, TextStyle::Outlined(mark, ui_color::kBackground, alpha));
      }
      break;
    }
    case PieceType::Flipper: {
      art::Medallion(canvas, c, 22.0f * s, alpha);
      float t = look.arm >= 0.0f ? look.arm : static_cast<float>(look.setting);
      t = std::clamp(t, 0.0f, 1.0f);
      const glm::vec2 dir = Normal(out_dir(0) * (1.0f - t) + out_dir(1) * t);
      canvas.Line(c - dir * (4.0f * s), c + dir * (12.0f * s), 4.4f * s, mark);
      canvas.Circle(c, 3.4f * s, mark);
      const glm::vec2 other = Normal(out_dir(0) * t + out_dir(1) * (1.0f - t));
      canvas.Ring(c + other * (10.5f * s), 2.6f * s, 1.4f * s, WithAlpha(brass::kIndicator, 0.5f * alpha));
      break;
    }
    case PieceType::Overflow: {
      const glm::vec2 h(21.0f * s);
      art::Plate(canvas, {c - h, c + h}, 9.0f * s, 4.5f * s, alpha);
      canvas.Line(c + glm::vec2(-10.0f, -9.0f) * s, c + glm::vec2(-10.0f, 7.0f) * s, 2.2f * s,
                  WithAlpha(brass::kIndicator, 0.5f * alpha));
      canvas.Line(c + glm::vec2(-10.0f, 7.0f) * s, c + glm::vec2(10.0f, 7.0f) * s, 2.2f * s,
                  WithAlpha(brass::kIndicator, 0.5f * alpha));
      canvas.Line(c + glm::vec2(10.0f, 7.0f) * s, c + glm::vec2(10.0f, -9.0f) * s, 2.2f * s,
                  WithAlpha(brass::kIndicator, 0.5f * alpha));
      if (font) {
        const int n = look.remaining >= 0 ? look.remaining : piece.n;
        canvas.Text(*font, std::to_string(n), c + glm::vec2(0.0f, -1.5f * s), 17.0f * s, Align::Center,
                    TextStyle{.color = mark});
      }
      // Its way: an arrow out of the side the first N go.
      const glm::vec2 d = out_dir(look.setting);
      Arrow(canvas, c + d * (34.0f * s), d, s, accent);
      break;
    }
    case PieceType::Bridge: {
      art::Medallion(canvas, c, 22.0f * s, alpha);
      canvas.Arc(c + glm::vec2(0.0f, 9.0f * s), 12.0f * s, 3.4f * s, -1.15f, 2.3f, mark);
      canvas.Line(c + glm::vec2(-12.0f, 6.0f) * s, c + glm::vec2(12.0f, 6.0f) * s, 2.2f * s, mark);
      break;
    }
    case PieceType::Mixer: {
      art::Medallion(canvas, c, 22.0f * s, alpha);
      canvas.Arc(c, 9.5f * s, 3.6f * s, look.spin, 2.0f, liquid(Color::Blue));
      canvas.Arc(c, 9.5f * s, 3.6f * s, look.spin + kPi, 2.0f, liquid(Color::Amber));
      canvas.Circle(c, 4.2f * s, liquid(Color::Green));
      break;
    }
    case PieceType::TwinGate: {
      const glm::vec2 h(24.0f * s, 16.0f * s);
      art::Plate(canvas, {c - h, c + h}, 8.0f * s, 4.0f * s, alpha);
      canvas.Line(c + glm::vec2(-9.0f, -8.0f) * s, c + glm::vec2(-9.0f, 8.0f) * s, 3.6f * s, mark);
      canvas.Line(c + glm::vec2(9.0f, -8.0f) * s, c + glm::vec2(9.0f, 8.0f) * s, 3.6f * s, mark);
      art::DashedLine(canvas, c + glm::vec2(-9.0f, 0.0f) * s, c + glm::vec2(9.0f, 0.0f) * s, 1.8f * s, mark, 3);
      break;
    }
  }
  if (look.locked) Padlock(canvas, c + glm::vec2(19.0f, -19.0f) * s, 0.8f * s, WithAlpha(ui_color::kMuted, alpha));
}

// ---- BoardView ----

void BoardView::SetLevel(const Level* level) {
  level_ = level;
  anims_.assign(level ? level->nodes.size() : 0, {});
  cup_fill_.assign(level ? level->nodes.size() : 0, 0.0f);
  bottle_fill_.assign(level ? level->nodes.size() : 0, {});
  for (size_t n = 0; level && n < level->nodes.size(); ++n) bottle_fill_[n].assign(level->nodes[n].slots.size(), 0.0f);
  markers_.clear();
  landed_.assign(level ? level->nodes.size() : 0, {});
  if (art_) art_->Invalidate();
}

void BoardView::SetArt(BoardArt* art) {
  art_ = art;
  if (art_) art_->Invalidate();
}

void BoardView::ClearMarkers() {
  markers_.clear();
  for (NodeAnim& anim : anims_) anim.tick = -1000000;
  for (std::vector<barges::Landing>& landed : landed_) landed.clear();
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
      case EventType::Pour: anims_[static_cast<size_t>(e.node)].tick = board.Ticks(); break;
      case EventType::Leave:
        anims_[static_cast<size_t>(e.node)].tick = board.Ticks();
        if (level_->nodes[static_cast<size_t>(e.node)].kind == NodeKind::Mill)
          anims_[static_cast<size_t>(e.node)].spin_target += kPi / 3.0f;  // a notch
        break;
      case EventType::Mix: anims_[static_cast<size_t>(e.node)].flash = 1.0f; break;
      case EventType::Flip:
        anims_[static_cast<size_t>(e.node)].flash = 1.0f;
        anims_[static_cast<size_t>(e.node)].tick = board.Ticks();
        break;
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
        if (node.kind == NodeKind::Cup) landed_[static_cast<size_t>(e.node)].push_back({e.bead, board.Ticks()});
        break;
      }
      case EventType::Spill: add(Marker::Spill, e.node, e.slot); break;
      case EventType::CupFull: add(Marker::Paid, e.node, e.slot); break;
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

void BoardView::Settle(const Board& board) {
  if (!level_) return;
  for (Marker& marker : markers_) marker.age += 100.0f;
  Update(0.0f, board);  // the racks' turns; faded near misses go
  for (size_t n = 0; n < anims_.size(); ++n) {
    anims_[n].spin = anims_[n].spin_target;
    anims_[n].flash = 0.0f;
    if (level_->nodes[n].kind == NodeKind::Cup && board.Started())
      cup_fill_[n] = static_cast<float>(board.Cup(static_cast<int>(n)).count);
    for (size_t s = 0; s < bottle_fill_[n].size() && board.Started(); ++s)
      bottle_fill_[n][s] = static_cast<float>(board.Bottle(static_cast<int>(n), static_cast<int>(s)).count);
  }
}

float BoardView::MomentsLeft() const {
  float left = 0.0f;
  if (frame_.calm) return left;  // every moment shows its end at once
  for (const Marker& marker : markers_) {
    if (art_) left = std::max(left, art_->MomentLength(marker) - marker.age);
  }
  return left;
}

void BoardView::BeginFrame(const Board& board, const Palette& palette, const wlw::ui::Font& font, float time, float dt,
                           bool calm, bool editing) {
  frame_.level = level_;
  frame_.board = &board;
  frame_.layout = layout_;
  frame_.palette = &palette;
  frame_.font = &font;
  frame_.time = calm ? 0.0f : time;
  frame_.dt = dt;
  frame_.calm = calm;
  frame_.editing = editing;
  frame_.anims = anims_;
  frame_.cup_fill = cup_fill_;
  frame_.bottle_fill = bottle_fill_;
  frame_.landed = landed_;
  frame_.markers = markers_;
}

void BoardView::DrawBack(Canvas& picture) {
  if (level_ && art_) art_->DrawBack(picture, frame_);
}

void BoardView::DrawLiquid(Liquid& liquid, float alpha) {
  frame_.alpha = alpha;
  if (level_ && art_) art_->DrawLiquid(liquid, frame_, alpha);
}

void BoardView::DrawDrops(Canvas& picture, float alpha) {
  frame_.alpha = alpha;
  if (level_ && art_) art_->DrawDrops(picture, frame_);
}

glm::vec2 BoardView::MarkerCanvas(int node, int slot) const {
  if (slot >= 0 && art_ && level_->nodes[static_cast<size_t>(node)].kind == NodeKind::Rack)
    return layout_.ToCanvas(art_->BottleAt(frame_, node, slot));
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

void BoardView::DrawParts(Canvas& canvas, const Options& options) {
  if (!level_ || !art_) return;
  const Board& board = *frame_.board;
  const Palette& palette = *frame_.palette;
  const wlw::ui::Font& font = *frame_.font;
  const float s = layout_.scale;
  const float time = frame_.time;
  const Rgba accent = art_->Accent();
  art_->DrawFront(canvas, frame_);

  // A fork with no router on it shows its flap (where everything goes). A piece that leaves the flap in
  // charge (a tank, a turnstile...) is drawn over it, so the flap is drawn again outside it below.
  auto draw_flap = [&](int n, bool covered) {
    const Node& node = level_->nodes[static_cast<size_t>(n)];
    const glm::vec2 c = layout_.ToCanvas(node.at);
    const glm::vec2 d = OutDir(*level_, node, static_cast<size_t>(std::max(0, board.Flap(n))));
    const glm::vec2 handle = FlapHandle(board, n);
    canvas.Line(covered ? c + d * (25.0f * s) : c, handle - d * (1.0f * s), 3.4f * s, brass::kIndicator);
    if (!covered) canvas.Circle(c, 3.4f * s, brass::kIndicator);
    if (node.flap_tappable) {
      canvas.Circle(handle, 5.0f * s, accent);
      canvas.Ring(handle, 5.0f * s, 1.5f * s, ui_color::kBackground);
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
    float a = frame_.editing ? 0.4f + 0.14f * std::sin(time * 3.0f) : 0.18f;
    float width = 2.0f * s;
    if (dragging) a = 0.9f;
    if (target) {
      a = 1.0f;
      width = 3.2f * s;
    }
    const float r = art_->SocketRadius(node) * s;
    const Rgba color = WithAlpha(dragging ? Mixed(accent, Hex(0xFFFFFF), 0.3f) : accent, a);
    if (target) canvas.Circle(c, r + 4.0f * s, WithAlpha(accent, 0.16f));
    for (int dash = 0; dash < 12; ++dash) canvas.Arc(c, r, width, static_cast<float>(dash) * kPi / 6.0f, 0.3f, color);
    if (node.twin >= 0 && board.PieceAt(node.twin) < 0) {
      const glm::vec2 other = NodeCanvas(node.twin);
      for (int dash = 0; dash < 12; ++dash)
        canvas.Arc(other, r, width, static_cast<float>(dash) * kPi / 6.0f, 0.3f, color);
      const glm::vec2 d = Normal(other - c);
      art::DashedLine(canvas, c + d * (r + 2.0f * s), other - d * (r + 2.0f * s), width, color, 4);
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
      look.alpha = 0.32f + 0.1f * std::sin(time * 4.0f);
      look.accent = accent;
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
    look.accent = accent;
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
    const float pulse = 0.5f + 0.5f * std::sin(time * 6.0f);
    canvas.Ring(NodeCanvas(options.hint_node), (30.0f + 4.0f * pulse) * s, 3.0f * s, WithAlpha(ui_color::kStar, 0.9f));
  }
}

void BoardView::DrawMarkers(Canvas& canvas) {
  if (!level_ || !art_) return;
  const Board& board = *frame_.board;
  const wlw::ui::Font& font = *frame_.font;
  const float s = layout_.scale;
  const float time = frame_.time;
  for (const Marker& marker : markers_) {
    if (art_->DrawMarker(canvas, frame_, marker)) continue;
    const float pop = std::min(1.0f, marker.age * 6.0f);
    switch (marker.kind) {
      case Marker::Mud: {
        const glm::vec2 c = NodeCanvas(marker.node);
        art_->DrawMud(canvas, c, marker.age, s, time);
        canvas.Text(font, "mud", c + glm::vec2(0.0f, 38.0f * s), 17.0f * s, Align::Center,
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
        const glm::vec2 top = art_->CupTop(frame_, marker.node);
        canvas.Text(font, text, top, 15.0f * s, Align::Center,
                    TextStyle::Outlined(ui_color::kBad, ui_color::kBackground));
        if (marker.kind == Marker::Spill) {
          const wlw::ui::Rect pool = art_->CupPool(frame_, marker.node);
          for (int k = 0; k < 3; ++k) {
            const float y = std::fmod(time * 40.0f + static_cast<float>(k) * 20.0f, 60.0f) * s;
            canvas.Circle({pool.max.x + 5.0f * s, pool.min.y + y}, 3.0f * s, kMud);
          }
        }
        break;
      }
      case Marker::Paid: break;  // the world's art shows it, if it does
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

void BoardView::DrawSpotlight(Canvas& canvas, int node, int slot, float amount) const {
  if (!level_ || !art_ || node < 0 || amount <= 0.0f) return;
  const glm::vec2 c = art_->Spotlight(frame_, node, slot);
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
