#pragma once

#include <string>
#include <vector>

#include "threadlock_rules.h"

// World 2's shop, in board units (plain C++: the level tool checks levels against it, the art draws it).
// The bottles stand on the counter; the orders they fill hang as slips on a cork board in the top right.
namespace threadlock::apothecary {

// A bottle as drawn: the drops land in its mouth (the node), it stands this far below it.
inline constexpr float kBottleBase = 52.0f;  // a cup's bottle
inline constexpr float kRackBase = 102.0f;   // a rack's front bottle, under its tap (the node)
inline constexpr float kCounterLip = 23.0f;  // the counter's top surface starts this far above the bottles' feet

struct Box {
  Vec2 min, max;
  bool Empty() const { return max.x <= min.x || max.y <= min.y; }
  bool Contains(Vec2 p) const { return p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y; }
};

// One order on the cork board: a cup (slot = -1) or a rack's bottle with an "order" name, in node order.
struct Order {
  int node = -1;
  int slot = -1;
  std::string name;
  Color want = Color::Blue;
  int count = 0;
};
std::vector<Order> Orders(const Level& level);

// The cork board for this many slips (empty for none): pinned to the wall's top right.
Box CorkBoard(int slips);
inline constexpr float kSlipWidth = 38.0f, kSlipHeight = 58.0f, kSlipStep = 42.0f;
// Where slip k's pin is (its top middle).
Vec2 SlipPin(int slips, int k);

// The top of the counter's surface: just above the feet of the lowest bottles.
float CounterTop(const Level& level);

}  // namespace threadlock::apothecary
