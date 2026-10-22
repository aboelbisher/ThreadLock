#pragma once

#include <optional>
#include <string>
#include <vector>

#include "polyline.h"
#include "threadlock_rules.h"

// World 3's canal city, in board units (plain C++: the level tool checks levels against it, the art draws it).
// The pipes are canals: straight stretches across or up and down with round bends, wide enough for a barge.
// Each cup is a harbour serving a district ("district": "Lantern Row"); a mill's rods run along the streets
// between the canals to the gates (locks) it works.
namespace threadlock::canal {

inline constexpr float kCanalWidth = 28.0f;     // bank to bank
inline constexpr float kBendRadius = 16.0f;     // a round bend takes this much of each stretch it joins
inline constexpr size_t kMaxDistrictName = 14;  // a harbour's pill (92 wide) fits its name and count

// A harbour (a cup) and the district it serves, in node order.
struct District {
  int node = -1;
  std::string name;
};
std::vector<District> Districts(const Level& level);
// A canal map: a harbour names its district (the marker, as World 2's shelves are the shop's).
bool IsCanalMap(const Level& level);

// A pipe's way along its canal, in board units: from node centre to node centre through its round bends.
// Distances are the rules' (the pipe's `points`, node edge to node edge); the canal's own length differs
// by `stretch` (a bend's arc is longer than the corner it rounds).
struct Way {
  art::Path whole;
  float lead = 0.0f;     // where the rules' pipe starts along `whole`
  float stretch = 1.0f;  // the rounded line's length / the rules' length
  // Along the rules' pipe (0 = its start; below 0 = back into the node it leaves).
  Vec2 PointAt(float distance) const;
  Vec2 DirectionAt(float distance) const;  // unit
};
Way WayOf(const Level& level, int pipe);
std::vector<Way> Ways(const Level& level);  // per pipe

// A canal's centre line: from its node's centre through its bends to the next node's centre.
std::vector<Vec2> CanalPoints(const Level& level, const Pipe& pipe);

// What's wrong with a canal map (empty = good; LookProblems checks the slants): a harbour without a district
// (or a name over 14 letters), or two for one; a stretch with no room for its round bends, or a canal doubling
// back; a lock, mill or gate not on a straight stretch; a canal running through a node it doesn't join,
// alongside another canal (or itself) or across one outside a crossing; a rod at a slant, off the board or
// along a canal. And room for the city's buildings: a lock's chamber and its canal in from the edge of the
// board, a harbour's basin and its district's name under it (on the board), a mill's house, none of them on a
// canal or a node.
std::vector<std::string> Problems(const Level& level);

// ---- The city round the machine (board units; the art draws it, Problems keeps room for it) ----

// A rectangle (y down).
struct Box {
  Vec2 min, max;
  bool Contains(Vec2 p) const { return p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y; }
  bool Overlaps(const Box& o) const { return max.x > o.min.x && min.x < o.max.x && max.y > o.min.y && min.y < o.max.y; }
  Box Grown(float by) const { return {{min.x - by, min.y - by}, {max.x + by, max.y + by}}; }
};

// A node's own frame: `u` = the way the canal runs through it (a unit along an axis), `w` = u turned a quarter
// clockwise (y down: u down -> w left). A point `along` u and `across` w from the node.
struct Frame {
  Vec2 at, u, w;
  Vec2 Point(float along, float across) const {
    return {at.x + u.x * along + w.x * across, at.y + u.y * along + w.y * across};
  }
  // The box between two distances along and two across.
  Box Span(float along0, float along1, float across0, float across1) const;
};

// A lock (a spout): barges wait moored in its chamber, from 30 behind the node to 18 ahead, 15 to each side;
// its canal comes in from beyond the edge of the board behind it (`stub`: from out there to the chamber).
inline constexpr float kLockBack = 30.0f, kLockFront = 18.0f, kLockHalf = 15.0f;
struct Lock {
  Frame frame;  // u = its canal's first stretch
  Box chamber;
  std::vector<Vec2> stub;
};
Lock LockAt(const Level& level, int spout);

// A harbour (a cup): a basin from 12 before the node to 60 past it (18 more per extra row of six berths),
// 46 to each side, and its district's name in a pill under it.
inline constexpr float kBasinHalf = 46.0f, kBasinBack = 12.0f, kBasinDeep = 60.0f, kBerthRow = 18.0f;
inline constexpr int kBerthsPerRow = 6;
struct Harbour {
  Frame frame;  // u = its canal's last stretch
  int rows = 1;
  Box basin, pill;
  float Deep() const { return kBasinDeep + kBerthRow * static_cast<float>(rows - 1); }
  // Berth k's middle: rows of six across the basin, the first row nearest the canal.
  Vec2 Berth(int k) const;
};
Harbour HarbourAt(const Level& level, int cup);

// A mill: its wheel across the canal, its house on the bank away from its rod (`v` = the rod's first way,
// else w), 20 to 56 from the canal's middle and 22 along each way.
struct Mill {
  Frame frame;  // u = the canal's way, w = v
  Box house;
};
Mill MillAt(const Level& level, int mill);

// A gate: mitre leaves across the canal, a lamp post on the bank its rod comes to (w), 12 downstream.
struct Gate {
  Frame frame;  // u = the canal's way, w = towards the rod (else a quarter clockwise)
  Vec2 lamp;
  Box post;
};
Gate GateAt(const Level& level, int gate);

// A mill's rod (`rod` = RodPoints: the mill, its bends, the gate): a pulse runs from the mill to the gate in one
// tick. `since` 0..1 = its place along the rod (by length); none outside 0..1.
std::optional<Vec2> PulseAt(const std::vector<Vec2>& rod, float since);
// A mill's wheel, in radians: a notch (pi/3) per drop that left, plus the share of the drop inside now (0..1 of
// its dwell), so the wheel turns on through a drop's dwell and is continuous as it leaves.
float WheelAngle(int turns, float inside);

// A jam's picture on the canals. A red lamp sits on each stuck node, beside it (kLampOffset up and right of its
// centre, off the water's edge).
inline constexpr float kLampOffset = 20.0f;
Vec2 LampAt(const Level& level, int node);
// The circle of waiting (JamReport::cycle; `lonely` = a shut gate nothing opens): the shortest way along the canals
// and the mills' rods from each node of the circle to the next, as one closed loop inset kLoopInset toward its
// middle. A shut gate brings its mill (and the rod) in before it. Empty when the nodes aren't joined.
inline constexpr float kLoopInset = 12.0f;
std::vector<Vec2> JamLoop(const Level& level, const std::vector<int>& cycle, int lonely);

// The city's still picture: rooftops in the gaps between the canals, a park, lamps along the quays. Generated
// from the level's id (the same city every time), over the board and a screen's margin round it.
struct Roof {
  Box box;
  int tone = 0;               // one of five slate tones
  bool across = true;         // its ridge runs across (else up and down)
  Vec2 chimney;               // its top left corner (3 x 3)
  std::vector<Vec2> windows;  // lit skylights (1.8 square, top left)
};
struct Tree {
  Vec2 at;
  float r = 0.0f;
};
struct City {
  std::vector<Roof> roofs;
  std::vector<Tree> trees;
  Box park{};               // empty (min == max) when no block was free
  std::vector<Vec2> lamps;  // along the quays
};
inline constexpr float kRoofClear = 17.0f;  // from a canal's middle line
City MakeCity(const Level& level);
// Every canal as the city draws it: each pipe's CanalPoints, a lock's stub first (pipe order).
std::vector<std::vector<Vec2>> CityCanals(const Level& level);
// What the rooftops keep clear of besides the canals: locks, basins + pills, mills' houses, rods, gate lamps.
std::vector<Box> KeepOuts(const Level& level);

}  // namespace threadlock::canal
