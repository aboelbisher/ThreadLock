#include "apothecary_look.h"

#include <algorithm>
#include <cmath>

namespace threadlock::apothecary {
namespace {

constexpr float kBoardRight = 356.0f, kBoardTop = 2.0f, kBoardPad = 5.0f;
constexpr float kBoardHeight = 72.0f;
constexpr int kMaxSlips = 4;        // more share the same board, overlapping
constexpr float kShelfFace = 9.0f;  // a shelf's top face (a jar's feet stand on it)

// The slate: chalk starts this far in, a five takes kGroupStep, a row of ten kRowStep down.
constexpr float kSlatePad = 3.0f, kSlateTop = 6.0f, kGroupStep = 13.0f, kRowStep = 15.0f, kStrikeOver = 2.0f;
constexpr Vec2 kSlateMin{24.0f, 30.0f};

float Step(int slips) {
  return slips <= kMaxSlips ? kSlipStep : kSlipStep * (kMaxSlips - 1) / static_cast<float>(slips - 1);
}

// The x of mark j (0..3) of five g in a row, from the slate's left edge.
float MarkX(int g, int j) {
  return kSlatePad + kStrikeOver + static_cast<float>(g) * kGroupStep + static_cast<float>(j) * kChalkStep;
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

Vec2 BellHang(int slips) {
  const Box board = CorkBoard(slips);
  return {board.min.x - 18.0f, board.min.y + 8.0f};
}

Box BellBox(int slips) {
  if (slips <= 0) return {};
  // The bracket reaches 26 in from the wall on the left; the bell is 28 wide, its clapper 37 below.
  const Vec2 h = BellHang(slips);
  return {{h.x - 27.0f, h.y - 9.0f}, {h.x + 15.0f, h.y + 38.0f}};
}

float CounterTop(const Level& level) {
  float top = -1.0f;
  for (const Node& node : level.nodes) {
    if (node.kind == NodeKind::Cup) top = std::max(top, node.at.y + kBottleBase - kBottleLip);
    if (node.kind == NodeKind::Rack) top = std::max(top, node.at.y + kRackBase - kRackLip);
  }
  return top >= 0.0f ? top : 467.0f;
}

Vec2 SlateSize(int marks) {
  const int in_row = std::clamp(marks, 1, 10);
  const int last = in_row - 1, g = last / 5, j = last % 5;
  const float right = j == 4 ? MarkX(g, 3) + kStrikeOver : MarkX(g, j);
  const int rows = std::max(1, (marks + 9) / 10);
  return {std::max(kSlateMin.x, right + kSlatePad + kStrikeOver),
          kSlateMin.y + kRowStep * static_cast<float>(rows - 1)};
}

ChalkMark ChalkAt(const Box& slate, int k) {
  const int row = k / 10, i = k % 10, g = i / 5, j = i % 5;
  const float top = slate.min.y + kSlateTop + kRowStep * static_cast<float>(row);
  if (j == 4) return {{slate.min.x + MarkX(g, 0) - kStrikeOver, top}, true, slate.min.x + MarkX(g, 3) + kStrikeOver};
  return {{slate.min.x + MarkX(g, j), top}};
}

Jar JarAt(const Level& level, int spout) {
  const Node& node = level.nodes[static_cast<size_t>(spout)];
  Vec2 next{node.at.x, node.at.y + 1.0f};
  if (!node.outs.empty()) {
    const Pipe& pipe = level.pipes[static_cast<size_t>(node.outs[0])];
    next = pipe.via.empty() ? level.nodes[static_cast<size_t>(pipe.to)].at : pipe.via.front();
  }
  const Vec2 slate = SlateSize(static_cast<int>(node.beads.size()));
  // Jar-local x (the tap's way positive) -> board x.
  Jar jar;
  auto span = [&](float a, float b, float y0, float y1) {
    const float x0 = jar.at.x + jar.side * a, x1 = jar.at.x + jar.side * b;
    return Box{{std::min(x0, x1), jar.at.y + y0}, {std::max(x0, x1), jar.at.y + y1}};
  };
  const float dx = next.x - node.at.x, dy = next.y - node.at.y;
  if (std::fabs(dx) > std::fabs(dy)) {
    jar.at = node.at;
    jar.side = dx > 0.0f ? 1.0f : -1.0f;
    jar.copper = node.radius;
  } else {
    // Beside the node, on its left unless the slate wouldn't fit there; high enough for its knob.
    jar.down = true;
    const float reach = 36.0f + 44.0f + slate.x;  // node -> the slate's far edge
    const float beside = node.at.x - reach >= 2.0f ? -1.0f : 1.0f;
    jar.at = {node.at.x + beside * 36.0f, std::max(node.at.y, 66.0f)};
    jar.side = -beside;
    jar.copper = jar.at.y - node.at.y + 7.0f;
  }
  jar.body = span(-40.0f, 12.0f, -50.0f, 16.0f);
  jar.whole = span(-40.0f, 24.0f, -64.0f, 16.0f);
  if (jar.down) {
    jar.whole.min.x = std::min(jar.whole.min.x, node.at.x - 6.0f);
    jar.whole.max.x = std::max(jar.whole.max.x, node.at.x + 6.0f);
  }
  jar.slate = span(-44.0f - slate.x, -44.0f, -36.0f, -36.0f + slate.y);
  jar.feet = jar.at.y + 16.0f;
  for (const float shelf : level.look.shelves)
    jar.on_shelf = jar.on_shelf || (jar.feet >= shelf - 0.5f && jar.feet <= shelf + kShelfFace + 0.5f);
  return jar;
}

}  // namespace threadlock::apothecary
