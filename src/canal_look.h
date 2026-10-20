#pragma once

#include <string>
#include <vector>

#include "threadlock_rules.h"

// World 3's canal city, in board units (plain C++: the level tool checks levels against it, the art draws it).
// The pipes are canals: straight stretches across or up and down with round bends, wide enough for a barge.
// Each cup is a harbour serving a district ("district": "Lantern Row"); a mill's rods run along the streets
// between the canals to the gates (locks) it works.
namespace threadlock::canal {

inline constexpr float kCanalWidth = 28.0f;  // bank to bank
inline constexpr float kBendRadius = 16.0f;  // a round bend takes this much of each stretch it joins

// A harbour (a cup) and the district it serves, in node order.
struct District {
  int node = -1;
  std::string name;
};
std::vector<District> Districts(const Level& level);
// A canal map: a harbour names its district (the marker, as World 2's shelves are the shop's).
bool IsCanalMap(const Level& level);

// A canal's centre line: from its node's centre through its bends to the next node's centre.
std::vector<Vec2> CanalPoints(const Level& level, const Pipe& pipe);

// What's wrong with a canal map (empty = good; LookProblems checks the slants): a harbour without a district,
// or two for one; a stretch with no room for its round bends, or a canal doubling back; a lock, mill or gate
// not on a straight stretch; a canal running through a node it doesn't join, alongside another canal (or
// itself) or across one outside a crossing; a rod at a slant, off the board or along a canal.
std::vector<std::string> Problems(const Level& level);

}  // namespace threadlock::canal
