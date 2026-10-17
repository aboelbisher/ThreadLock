#include "workshop_art.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "liquid.h"

namespace threadlock {
namespace {

using art::kPi;
using wlw::ui::Align;
using wlw::ui::Canvas;
using wlw::ui::TextStyle;

constexpr float kCupHalf = 24.0f, kCupHeight = 62.0f;

glm::vec2 Normal(glm::vec2 v) {
  const float length = glm::length(v);
  return length > 1e-5f ? v / length : glm::vec2(0.0f, 1.0f);
}

glm::vec2 Perp(glm::vec2 v) { return {-v.y, v.x}; }

}  // namespace

void DrawBead(Canvas& canvas, const Bead& bead, glm::vec2 c, float r, const Palette& palette, float alpha) {
  canvas.Circle(c, r, WithAlpha(LiquidColor(palette, bead.a), alpha));
  if (bead.IsBlend()) canvas.Arc(c, r * 0.5f, r, 0.0f, kPi, WithAlpha(LiquidColor(palette, bead.b), alpha));
}

Vec2 BottleAt(const ArtFrame& frame, int rack, int slot) {
  const Node& node = frame.level->nodes[static_cast<size_t>(rack)];
  const int k = std::max(1, static_cast<int>(node.slots.size()));
  const float radius = k <= 2 ? 22.0f : 30.0f;
  const float steps = frame.anims[static_cast<size_t>(rack)].spin;
  const float a = -kPi * 0.5f + 2.0f * kPi * (static_cast<float>(slot - node.first) - steps) / static_cast<float>(k);
  return {node.at.x + std::cos(a) * radius, node.at.y + radius + std::sin(a) * radius};
}

void DrawMachinePart(Canvas& canvas, const ArtFrame& frame, int n) {
  const Level& level = *frame.level;
  const Board& board = *frame.board;
  const Node& node = level.nodes[static_cast<size_t>(n)];
  const float s = frame.Scale();
  const glm::vec2 c = frame.At(n);
  const Rgba ink = ui_color::kInk;
  switch (node.kind) {
    case NodeKind::Rack: {
      // A turntable of bottles seen from above; the one under the pipe is at the top. Each bottle is
      // a cup of its own.
      const int k = static_cast<int>(node.slots.size());
      const float radius = k <= 2 ? 22.0f : 30.0f;
      const glm::vec2 centre = frame.layout.ToCanvas({node.at.x, node.at.y + radius});
      canvas.Circle(centre, (radius + 17.0f) * s, WithAlpha(ui_color::kPanel, 0.75f));
      canvas.Ring(centre, (radius + 17.0f) * s, 2.0f * s, WithAlpha(ink, 0.3f));
      canvas.Circle(centre, 4.0f * s, WithAlpha(ink, 0.5f));
      for (int slot = 0; slot < k; ++slot) {
        const Slot& want = node.slots[static_cast<size_t>(slot)];
        const CupState& bottle = board.Started() ? board.Bottle(n, slot) : CupState{};
        const glm::vec2 b = frame.layout.ToCanvas(BottleAt(frame, n, slot));
        const Rgba color = bottle.impure ? kMud : LiquidColor(*frame.palette, want.want);
        const float fill = std::clamp(frame.bottle_fill[static_cast<size_t>(n)][static_cast<size_t>(slot)] /
                                          static_cast<float>(std::max(1, want.count)),
                                      0.0f, 1.0f);
        canvas.Circle(b, 12.5f * s, WithAlpha(ui_color::kPipeInside, 0.95f));
        if (fill > 0.01f) canvas.Circle(b, 11.0f * s * std::sqrt(fill), WithAlpha(color, 0.85f));
        canvas.Ring(b, 12.5f * s, 2.5f * s, color);
        char text[24];
        std::snprintf(text, sizeof(text), "%d/%d", bottle.count, want.count);
        canvas.Text(*frame.font, text, b + glm::vec2(0.0f, 4.5f * s), 12.0f * s, Align::Center,
                    TextStyle::Outlined(bottle.impure || bottle.spilled ? ui_color::kBad : ink, ui_color::kBackground));
      }
      break;
    }
    case NodeKind::Mill: {
      // A wheel that turns a notch per drop; dashed gears run to the gates it flips.
      const NodeAnim& anim = frame.anims[static_cast<size_t>(n)];
      for (int gate : node.gears)
        art::DashedLine(canvas, c, frame.At(gate), 1.5f * s, WithAlpha(ui_color::kStar, 0.3f + 0.5f * anim.flash), 12);
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
      const bool open = board.GateOpen(n);
      glm::vec2 d{0.0f, 1.0f};
      if (!node.ins.empty()) {
        const Pipe& pipe = level.pipes[static_cast<size_t>(node.ins[0])];
        d = Normal(glm::vec2(node.at.x, node.at.y) - glm::vec2(pipe.points.back().x, pipe.points.back().y));
      }
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
      canvas.Circle(lamp, (4.5f + 3.0f * frame.anims[static_cast<size_t>(n)].flash) * s,
                    open ? ui_color::kGood : ui_color::kBad);
      canvas.Ring(lamp, 4.5f * s, 1.5f * s, ui_color::kBackground);
      break;
    }
    default: break;
  }
}

// ---- The workshop look ----

void WorkshopArt::DrawBack(Canvas& canvas, const ArtFrame& frame) {
  const Level& level = *frame.level;
  const Layout& layout = frame.layout;
  const float s = frame.Scale();
  std::vector<std::vector<glm::vec2>> lines;
  for (const Pipe& pipe : level.pipes) {
    // From node centre to node centre, so pipes reach into junctions and sockets.
    std::vector<glm::vec2> points{layout.ToCanvas(level.nodes[static_cast<size_t>(pipe.from)].at)};
    for (Vec2 p : pipe.via) points.push_back(layout.ToCanvas(p));
    points.push_back(layout.ToCanvas(level.nodes[static_cast<size_t>(pipe.to)].at));
    lines.push_back(std::move(points));
  }
  auto polyline = [&](const std::vector<glm::vec2>& points, float thickness, const Rgba& color) {
    art::Polyline(canvas, points, thickness, color);
    for (size_t i = 1; i + 1 < points.size(); ++i) canvas.Circle(points[i], thickness * 0.5f, color);  // round joints
  };
  for (const auto& line : lines) polyline(line, 24.0f * s, ui_color::kPipeEdge);
  auto junction = [](NodeKind kind) {
    return kind == NodeKind::Cross || kind == NodeKind::Merge || kind == NodeKind::Mill;
  };
  for (const Node& node : level.nodes) {
    const glm::vec2 c = layout.ToCanvas(node.at);
    if (junction(node.kind)) canvas.Circle(c, 19.0f * s, ui_color::kPipeEdge);
    if (node.kind == NodeKind::Spout) canvas.Circle(c, 26.0f * s, ui_color::kPipeEdge);
    if (node.kind == NodeKind::Drain) canvas.Circle(c, 17.0f * s, ui_color::kPipeEdge);
  }
  for (const auto& line : lines) polyline(line, 17.0f * s, ui_color::kPipeInside);
  for (const Node& node : level.nodes) {
    const glm::vec2 c = layout.ToCanvas(node.at);
    if (junction(node.kind)) canvas.Circle(c, 15.0f * s, ui_color::kPipeInside);
    if (node.kind == NodeKind::Spout) canvas.Circle(c, 22.0f * s, ui_color::kPipeInside);
    if (node.kind == NodeKind::Drain) canvas.Circle(c, 14.0f * s, ui_color::kPipeInside);
    if (node.kind == NodeKind::Cup) {
      const glm::vec2 min = layout.ToCanvas({node.at.x - kCupHalf, node.at.y});
      const glm::vec2 max = layout.ToCanvas({node.at.x + kCupHalf, node.at.y + kCupHeight});
      canvas.FillRect({min, max}, WithAlpha(ui_color::kPipeInside, 0.9f), 6.0f * s);
    }
  }
  // Faint tick marks, one per cell: what you see is the timing.
  const Rgba tick = WithAlpha(ui_color::kInk, 0.07f);
  for (const Pipe& pipe : level.pipes) {
    for (int k = 1; k < pipe.cells; ++k) {
      const float d = pipe.length * static_cast<float>(k) / static_cast<float>(pipe.cells);
      const glm::vec2 p = layout.ToCanvas(PointAlong(pipe.points, d));
      const glm::vec2 q = layout.ToCanvas(PointAlong(pipe.points, d + 0.5f));
      const glm::vec2 n = Perp(Normal(q - p));
      canvas.Line(p + n * (6.0f * s), p + n * (8.0f * s), 1.2f * s, tick);
    }
  }
}

void WorkshopArt::DrawFront(Canvas& canvas, const ArtFrame& frame) {
  const Level& level = *frame.level;
  const Board& board = *frame.board;
  const Layout& layout = frame.layout;
  const wlw::ui::Font& font = *frame.font;
  const float s = frame.Scale();
  const Rgba ink = ui_color::kInk;
  for (size_t n = 0; n < level.nodes.size(); ++n) {
    const Node& node = level.nodes[n];
    const glm::vec2 c = layout.ToCanvas(node.at);
    switch (node.kind) {
      case NodeKind::Spout: {
        canvas.Ring(c, 22.0f * s, 2.5f * s, WithAlpha(ink, 0.55f));
        if (!node.beads.empty()) art::Glyph(canvas, node.beads[0].a, c, 4.0f * s, WithAlpha(ink, 0.8f));
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
          DrawBead(canvas, node.beads[static_cast<size_t>(poured + k)], layout.ToCanvas(at), (above ? 4.0f : 3.2f) * s,
                   *frame.palette, 1.0f);
        }
        break;
      }
      case NodeKind::Cup: {
        const CupState& cup = board.Started() ? board.Cup(static_cast<int>(n)) : CupState{};
        const Rgba color = cup.impure ? kMud : LiquidColor(*frame.palette, node.want);
        const glm::vec2 tl = layout.ToCanvas({node.at.x - kCupHalf, node.at.y});
        const glm::vec2 br = layout.ToCanvas({node.at.x + kCupHalf, node.at.y + kCupHeight});
        canvas.Line(tl, {tl.x, br.y}, 3.0f * s, color);
        canvas.Line({tl.x, br.y}, br, 3.0f * s, color);
        canvas.Line(br, {br.x, tl.y}, 3.0f * s, color);
        for (int k = 1; k < node.count; ++k) {
          const float y = node.at.y + kCupHeight - 3.0f -
                          (kCupHeight - 9.0f) * static_cast<float>(k) / static_cast<float>(node.count);
          canvas.Line(layout.ToCanvas({node.at.x + kCupHalf - 8.0f, y}),
                      layout.ToCanvas({node.at.x + kCupHalf - 3.0f, y}), 1.4f * s, WithAlpha(ink, 0.3f));
        }
        char text[24];
        std::snprintf(text, sizeof(text), "%d/%d", cup.count, node.count);
        const glm::vec2 label = layout.ToCanvas({node.at.x + 4.0f, node.at.y + kCupHeight + 11.0f});
        canvas.Text(font, text, label, 15.0f * s, Align::Center,
                    TextStyle::Outlined(cup.impure || cup.spilled ? ui_color::kBad : ink, ui_color::kBackground));
        art::Glyph(canvas, node.want, label + glm::vec2(-20.0f * s, 0.0f), 4.0f * s,
                   LiquidColor(*frame.palette, node.want));
        break;
      }
      case NodeKind::Drain: {
        canvas.Ring(c, 14.0f * s, 2.5f * s, ui_color::kFaint);
        for (float dy : {-6.0f, 0.0f, 6.0f}) {
          canvas.Line(c + glm::vec2(-8.0f, dy) * s, c + glm::vec2(8.0f, dy) * s, 2.0f * s, ui_color::kFaint);
        }
        break;
      }
      case NodeKind::Rack:
      case NodeKind::Mill:
      case NodeKind::Gate: DrawMachinePart(canvas, frame, static_cast<int>(n)); break;
      default: break;
    }
  }
}

wlw::ui::Rect WorkshopArt::CupPool(const ArtFrame& frame, int cup) const {
  const Vec2 at = frame.level->nodes[static_cast<size_t>(cup)].at;
  return {frame.layout.ToCanvas({at.x - kCupHalf + 3.0f, at.y + 6.0f}),
          frame.layout.ToCanvas({at.x + kCupHalf - 3.0f, at.y + kCupHeight - 3.0f})};
}

void WorkshopArt::SpoutLiquid(Liquid& liquid, const ArtFrame& frame, int spout) const {
  // A slow swirl of what's still to pour.
  const Node& node = frame.level->nodes[static_cast<size_t>(spout)];
  const int left = static_cast<int>(node.beads.size()) - (frame.board->Started() ? frame.board->Poured(spout) : 0);
  if (left <= 0) return;
  const Bead& next = node.beads[node.beads.size() - static_cast<size_t>(left)];
  const glm::vec2 c = frame.At(spout);
  const float s = frame.Scale();
  for (int k = 0; k < 10; ++k) {
    const bool inner = k % 2 != 0;
    const float a = static_cast<float>(k / 2) / 5.0f * 2.0f * kPi + frame.time * (inner ? -1.6f : 1.1f);
    const float r = (inner ? 7.0f : 12.0f) * s;
    const Rgba color = LiquidColor(*frame.palette, (inner && next.IsBlend()) ? next.b : next.a);
    liquid.Blob(c + glm::vec2(std::cos(a), std::sin(a)) * r, {-std::sin(a), std::cos(a)}, 7.0f * s, color, color,
                static_cast<uint32_t>(9000 + spout * 16 + k));
  }
}

glm::vec2 WorkshopArt::CupTop(const ArtFrame& frame, int cup) const {
  const Vec2 at = frame.level->nodes[static_cast<size_t>(cup)].at;
  return frame.layout.ToCanvas({at.x, at.y - 10.0f});
}

void WorkshopArt::DrawMud(Canvas& canvas, glm::vec2 c, float age, float s, float) const {
  const float pop = std::min(1.0f, age * 6.0f);
  canvas.Circle(c, 13.0f * s * pop, kMud);
  for (int i = 0; i < 6; ++i) {
    const float a = kPi / 3.0f * static_cast<float>(i) + 0.35f;
    const float d = (21.0f + static_cast<float>(i % 2) * 5.0f) * s * pop;
    canvas.Circle(c + glm::vec2(std::cos(a), std::sin(a)) * d, (2.5f + static_cast<float>(i % 3)) * s, kMud);
  }
}

}  // namespace threadlock
