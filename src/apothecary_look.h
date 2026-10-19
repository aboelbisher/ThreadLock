#pragma once

#include <string>
#include <vector>

#include "threadlock_rules.h"

// World 2's shop, in board units (plain C++: the level tool checks levels against it, the art draws it).
// The bottles stand on the counter; the orders they fill hang as slips on a cork board in the top right,
// with the shop's bell beside it. Each spout is a stoneware jar with a chalk slate counting its drops.
namespace threadlock::apothecary {

// A bottle as drawn: the drops land in its mouth (the node), it stands this far below it.
inline constexpr float kBottleBase = 52.0f;  // a cup's bottle
inline constexpr float kRackBase = 102.0f;   // a rack's front bottle, under its tap (the node)
// How far down the counter's top surface the feet stand: a bottle on its face, a rack's turntable over
// its front edge.
inline constexpr float kBottleLip = 10.0f;
inline constexpr float kRackLip = 23.0f;
inline constexpr float kCounterFace = 14.0f;  // the counter's top surface, seen from above

struct Box {
  Vec2 min, max;
  bool Empty() const { return max.x <= min.x || max.y <= min.y; }
  bool Contains(Vec2 p) const { return p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y; }
  bool Overlaps(const Box& o) const {
    return !Empty() && !o.Empty() && max.x > o.min.x && min.x < o.max.x && max.y > o.min.y && min.y < o.max.y;
  }
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

// The cork board for this many slips (empty for none): pinned to the wall's top right, its top at `top`.
// A grand shop (a board taller than one screen) pins it at the top of the last screen, over the counter:
// CorkTop.
inline constexpr float kCorkTop = 2.0f;
float CorkTop(const Level& level);
Box CorkBoard(int slips, float top = kCorkTop);
inline constexpr float kSlipWidth = 38.0f, kSlipHeight = 62.0f, kSlipStep = 42.0f;
// Where slip k's pin is (its top middle).
Vec2 SlipPin(int slips, int k, float top = kCorkTop);
// The shop bell, on a bracket left of the cork board (empty with no orders), and the point it hangs from.
Box BellBox(int slips, float top = kCorkTop);
Vec2 BellHang(int slips, float top = kCorkTop);

// The top of the counter's surface: just above the feet of the lowest bottles.
float CounterTop(const Level& level);

// A rack is a lazy susan on the counter: its tap is the node, the turntable's centre kRackTable below it, its
// bottles round a ring with the one under the tap at the front. `spin` = the steps it has turned (ticks /
// period, animated): bottle first + spin is at the front. Seen from the front, a bottle further back stands
// higher up and smaller.
inline constexpr float kRackTable = 90.0f;
struct RackSpot {
  Vec2 mouth, base;    // where its mouth is, where it stands
  float scale = 1.0f;  // its size (1 at the front)
  float depth = 0.0f;  // -1 at the back .. +1 at the front: draw the smaller first
};
RackSpot RackBottle(const Node& rack, int slot, float spin);
// The turntable with its bottles and its shadow; the clock on the counter's front under it.
Box RackBox(const Node& rack);
Vec2 ClockAt(const Node& rack);
inline constexpr float kClockRadius = 17.0f;
// A cup bottle's brass tag on the counter's front.
Box BottleTag(const Node& cup, float counter);

// A spout's jar. Its own origin `at` is the mock's node: the body spans x -40..+12 from it (the tap's side
// positive), y -50..+16, lid and knob above to -64, the tap +10..+24 across. A spout whose first pipe runs
// across sits inside its jar's tap side (at = the node); one whose pipe runs down has the jar beside it,
// the tap's stem running across to it and turning down in a brass elbow. The slate hangs on the wall
// beside the jar, on the side away from the tap, and grows with the drops: rows of ten, in fives.
struct Jar {
  Vec2 at;
  float side = 1.0f;  // the way the tap points: +1 right
  bool down = false;  // the first pipe leaves downwards
  Box body;           // the glazed body
  Box whole;          // with the lid, the knob and the tap (and the stem and elbow)
  Box slate;
  float feet = 0.0f;
  bool on_shelf = false;  // stands on one of look.shelves (else on a bracket of its own)
  float copper = 0.0f;    // where the copper starts along the first pipe, from the node
};
Jar JarAt(const Level& level, int spout);
// The chalk tally: mark k (0 = the first drop) runs from `top` down kChalkTall; every fifth is a strike
// across the four before it, from its x to `strike_to`.
inline constexpr float kChalkTall = 12.0f, kChalkStep = 3.0f;
struct ChalkMark {
  Vec2 top;
  bool strike = false;
  float strike_to = 0.0f;
};
ChalkMark ChalkAt(const Box& slate, int k);
Vec2 SlateSize(int marks);

// ---- The moments, in seconds of real time (whatever the run's speed) ----
// An order filled: the cork drops into the bottle, the label slides onto it, the shop bell rings and the
// PAID stamp thumps onto its slip. `age` = seconds since it filled; reduced motion shows the end at once.
inline constexpr float kPaidLength = 1.6f;
inline constexpr float kPaidPop = 0.0f, kPaidBell = 0.7f, kPaidStamp = 1.3f;  // the sounds (3e): cork, bell, stamp
struct PaidMoment {
  float cork = 0.0f;   // 0..1: falling into the mouth
  float label = 0.0f;  // 0..1: sliding on from the left
  float stamp = 0.0f;  // 0..1: coming down onto the slip
};
PaidMoment PaidAt(float age, bool calm);
// The bell, `since` seconds after it was rung (< 0: not yet): its swing (radians, clockwise about where it
// hangs, dying away) and its sound waves (how far out beside it, how strong).
struct BellRing {
  float swing = 0.0f;
  float wave = 0.0f;
  float wave_alpha = 0.0f;
};
BellRing BellAt(float since, bool calm);
// A bottle ran over: the liquid runs over its shoulder and down its side, pools at its foot and drips off the
// counter's (or the turntable's) edge; a paid bottle's cork pops out. The run goes on: the moment stays.
inline constexpr float kSpillLength = 1.2f;
inline constexpr float kSpillSplash = 0.0f;  // the sound (3e)
struct SpillMoment {
  float stream = 0.0f;                 // 0..1: how far down the side
  float puddle = 0.0f;                 // 0..1: how far it has spread
  float drip_a = 0.0f, drip_b = 0.0f;  // 0..1: the two drips' lengths
  float cork_off = 0.0f;               // 0..1: a cork flying out
  float pill = 0.0f;                   // 0..1: the count's pill popping up
};
SpillMoment SpillAt(float age, bool calm);
// An order short at the end: a dashed ghost of what's missing fills in, the count's pill pops up. On the
// cork board an unpaid order's slip (short or spilled) droops on its pin, struck through, "unpaid".
inline constexpr float kShortLength = 1.5f;
inline constexpr float kShortDull = 0.0f;  // the sound (3e)
struct ShortMoment {
  float ghost = 0.0f;  // 0..1: the ghost fill's strength
  float pill = 0.0f;
};
ShortMoment ShortAt(float age, bool calm);
inline constexpr float kSlipDroop = 9.0f;  // degrees, about its pin
float UnpaidAt(float age, bool calm);      // 0..1: the droop, the strike and the word
// Ease out: fast, then settling (0..1 -> 0..1).
float EaseOut(float t);

}  // namespace threadlock::apothecary
