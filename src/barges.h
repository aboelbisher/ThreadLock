#pragma once

#include <vector>

#include "canal_look.h"
#include "threadlock_rules.h"

// World 3's barges (plain C++: tested, the art draws them). Where a drop is along its canal and which way it
// heads, how a harbour's landed barges glide into their berths, and when a lock's leaves swing.
namespace threadlock::barges {

// A drop's pose in board units: `heading` is a unit vector along the canal; `fade` is 0..1 (a drop that
// just poured slides out of its lock, fading in).
struct Pose {
  Vec2 at;
  Vec2 heading{0.0f, 1.0f};
  float fade = 1.0f;
  bool moving = false;  // it changed place this tick (a wake trails it)
};

// Where barges sit inside two-way canals' places. A basin gives each barge a berth of its own when it comes in and
// keeps it there until it leaves (the others never shuffle round); a passing place puts each barge on its own side of
// the canal, by the way it came. Update it after every tick (calling it each frame is cheap: it acts on a new tick);
// a drop keeps its spot for the tick it leaves, so it slides out of its berth, not out of the basin's middle.
class Moorings {
 public:
  void Update(const Level& level, const std::vector<canal::Way>& ways, const Board& board);
  bool Has(int drop_id, int node) const;
  Vec2 At(int drop_id) const;  // its spot (call Has first)
  void Clear();

 private:
  struct Spot {
    int node = -1;
    int berth = -1;  // a basin's berth (-1: a passing place's side)
    Vec2 at;
  };
  std::vector<std::pair<int, Spot>> spots_;  // per drop id (a few at a time: a flat list)
  int tick_ = -1;
  const Spot* Find(int drop_id) const;
};

// The berth `k` of a basin with `berths` berths, round its middle.
Vec2 BerthPoint(const Node& basin, int k);

// Where `drop` is, `alpha` (0..1) of the way from its last tick to the next. `ways` = canal::Ways(level). On a two-way
// map, pass its moorings: barges then slide into their berths and out of them.
Pose PoseOf(const Level& level, const std::vector<canal::Way>& ways, const Drop& drop, float alpha,
            const Moorings* moorings = nullptr);

// A barge that landed in a harbour: its colours and the tick it landed on.
struct Landing {
  Bead bead;
  int tick = 0;
};

// A lock's leaves: 0 shut .. 1 open, `since` = ticks since the lock's last pour (ramp open over the first
// quarter tick, held, then shut again by the time the next one is due).
float LockOpen(float since);

// A landed barge's glide from the basin's mouth into its berth: 0..1 (eased out) over 1.5 ticks, `since` =
// ticks since it landed.
float Glide(float since);

// A gate's leaves: 0 shut .. 1 open. `open_now` = the gate's state after its flip; the leaves swing to it over
// 0.6 tick from when the flip landed (`since_flip` = ticks since then; before it, the old state).
float GateOpenness(bool open_now, float since_flip);

// A harbour's payoff, `age` seconds after its order was filled: its lamps come on and its warm glow swells and
// dies over kWarmLength; the district's light runs out from the harbour over kDistrictLength (eased out).
inline constexpr float kWarmLength = 0.8f, kDistrictLength = 2.2f;
float LampsAt(float age);     // 0..1
float WarmAt(float age);      // 0..1, 0 at both ends
float DistrictAt(float age);  // 0..1

// A grand machine's payoff (the finale, two screens tall): once its last harbour is served, the whole city's light runs
// out from the payoff point over every roof on both screens, over kCityLength (eased out), and the roofs stay lit.
inline constexpr float kCityLength = 3.0f;
float CityAt(float age);  // 0..1

}  // namespace threadlock::barges
