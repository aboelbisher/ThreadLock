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

// Where `drop` is, `alpha` (0..1) of the way from its last tick to the next. `ways` = canal::Ways(level).
Pose PoseOf(const Level& level, const std::vector<canal::Way>& ways, const Drop& drop, float alpha);

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

}  // namespace threadlock::barges
