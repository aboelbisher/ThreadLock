#include "apothecary_look.h"

#include <algorithm>

namespace threadlock::apothecary {
namespace {

constexpr float kBoardRight = 356.0f, kBoardTop = 2.0f, kBoardPad = 5.0f;
constexpr float kBoardHeight = 72.0f;
constexpr int kMaxSlips = 4;  // more share the same board, overlapping

float Step(int slips) {
  return slips <= kMaxSlips ? kSlipStep : kSlipStep * (kMaxSlips - 1) / static_cast<float>(slips - 1);
}

}  // namespace

std::vector<Order> Orders(const Level& level) {
  std::vector<Order> orders;
  for (size_t n = 0; n < level.nodes.size(); ++n) {
    const Node& node = level.nodes[n];
    if (node.kind == NodeKind::Cup && !node.order.empty())
      orders.push_back({static_cast<int>(n), -1, node.order, node.want, node.count});
    if (node.kind != NodeKind::Rack) continue;
    for (size_t k = 0; k < node.slots.size(); ++k) {
      const Slot& slot = node.slots[k];
      if (!slot.order.empty())
        orders.push_back({static_cast<int>(n), static_cast<int>(k), slot.order, slot.want, slot.count});
    }
  }
  return orders;
}

Box CorkBoard(int slips) {
  if (slips <= 0) return {};
  const float width = Step(slips) * static_cast<float>(slips - 1) + kSlipWidth + 2.0f * kBoardPad;
  return {{kBoardRight - width, kBoardTop}, {kBoardRight, kBoardTop + kBoardHeight}};
}

Vec2 SlipPin(int slips, int k) {
  const Box board = CorkBoard(slips);
  return {board.min.x + kBoardPad + kSlipWidth * 0.5f + Step(slips) * static_cast<float>(k), board.min.y + 6.0f};
}

float CounterTop(const Level& level) {
  float feet = 0.0f;
  for (const Node& node : level.nodes) {
    if (node.kind == NodeKind::Cup) feet = std::max(feet, node.at.y + kBottleBase);
    if (node.kind == NodeKind::Rack) feet = std::max(feet, node.at.y + kRackBase);
  }
  return feet > 0.0f ? feet - kCounterLip : 467.0f;
}

}  // namespace threadlock::apothecary
