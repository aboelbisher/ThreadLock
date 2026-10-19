#include "board_art.h"

#include <algorithm>
#include <cmath>

#include "liquid.h"

namespace threadlock {
namespace {

glm::vec2 Normal(glm::vec2 v) {
  const float length = glm::length(v);
  return length > 1e-5f ? v / length : glm::vec2(0.0f, 1.0f);
}

float CellDistance(const Pipe& pipe, int cell) {
  return (static_cast<float>(cell) + 0.5f) * pipe.length / static_cast<float>(pipe.cells);
}

}  // namespace

glm::vec2 BoardArt::PipePoint(const ArtFrame& frame, int pipe, float distance) const {
  return frame.layout.ToCanvas(PointAlong(frame.level->pipes[static_cast<size_t>(pipe)].points, distance));
}

void BoardArt::DrawLiquid(Liquid& liquid, const ArtFrame& frame, float alpha) const {
  const Level& level = *frame.level;
  const Board& board = *frame.board;
  const Palette& palette = *frame.palette;
  const float s = frame.Scale();
  alpha = std::clamp(alpha, 0.0f, 1.0f);
  auto where = [&](const Drop& drop, const Loc& loc) {
    if (loc.kind == Loc::OnPipe) {
      return PipePoint(frame, loc.index, CellDistance(level.pipes[static_cast<size_t>(loc.index)], loc.cell));
    }
    return frame.layout.ToCanvas(DropPoint(level, drop, loc));
  };
  if (board.Started()) {
    for (const Drop& drop : board.Drops()) {
      if (!drop.alive) continue;
      glm::vec2 at, along;
      if (drop.prev.kind == Loc::OnPipe && drop.loc.kind == Loc::OnPipe && drop.prev.index == drop.loc.index) {
        // Along the pipe as drawn (round bends, coils), not across them.
        const Pipe& pipe = level.pipes[static_cast<size_t>(drop.loc.index)];
        const float d = CellDistance(pipe, drop.prev.cell) +
                        (CellDistance(pipe, drop.loc.cell) - CellDistance(pipe, drop.prev.cell)) * alpha;
        at = PipePoint(frame, drop.loc.index, d);
        along = PipePoint(frame, drop.loc.index, d + 1.0f) - at;
      } else {
        const glm::vec2 now = where(drop, drop.loc);
        const glm::vec2 before = drop.prev.kind == Loc::None ? now : where(drop, drop.prev);
        at = before + (now - before) * alpha;
        along = now - before;
        if (glm::length(along) < 1e-3f && drop.loc.kind == Loc::OnPipe) {
          const float d = CellDistance(level.pipes[static_cast<size_t>(drop.loc.index)], drop.loc.cell);
          along = PipePoint(frame, drop.loc.index, d + 1.0f) - PipePoint(frame, drop.loc.index, d);
        }
      }
      const Rgba front = LiquidColor(palette, drop.bead.a);
      const Rgba back = drop.bead.IsBlend() ? LiquidColor(palette, drop.bead.b) : front;
      liquid.Blob(at, Normal(along), DropSize() * s, front, back, static_cast<uint32_t>(drop.id) + 1u);
    }
  }
  for (size_t n = 0; n < level.nodes.size(); ++n) {
    const Node& node = level.nodes[n];
    if (node.kind == NodeKind::Spout) SpoutLiquid(liquid, frame, static_cast<int>(n));
    if (node.kind == NodeKind::Cup && frame.cup_fill[n] > 0.01f) CupLiquid(liquid, frame, static_cast<int>(n));
  }
}

void BoardArt::CupLiquid(Liquid& liquid, const ArtFrame& frame, int cup) const {
  const Node& node = frame.level->nodes[static_cast<size_t>(cup)];
  const CupState& state = frame.board->Started() ? frame.board->Cup(cup) : CupState{};
  const Rgba color = state.impure ? kMud : LiquidColor(*frame.palette, node.want);
  const wlw::ui::Rect pool = CupPool(frame, cup);
  liquid.Pool(pool.min, pool.max,
              frame.cup_fill[static_cast<size_t>(cup)] / static_cast<float>(std::max(1, node.count)), color,
              static_cast<uint32_t>(cup) * 7919u);
}

}  // namespace threadlock
