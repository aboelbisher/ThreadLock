#pragma once

#include <algorithm>
#include <vector>

#include "threadlock_rules.h"

// World 1's rose window (plain C++, no engine): which of its panes belong to which lantern. The art
// lights them (LanternArt::UpdatePanes) and the sounds chime as they light (sound_cues.h).
// The 12 outer panes are shared out among the level's lanterns in order; the 12 inner ones are the
// heart, lit when the machine is solved.
namespace threadlock {

inline constexpr int kOuterPanes = 12;
inline constexpr int kInnerPanes = 12;

struct PaneRange {
  int first = 0, last = 0;  // [first, last)
  int Count() const { return last - first; }
};

// The panes of the lantern (cup) at `cup_node`; empty if it isn't a cup.
inline PaneRange LanternPanes(const Level& level, int cup_node) {
  int index = -1, cups = 0;
  for (size_t n = 0; n < level.nodes.size(); ++n) {
    if (level.nodes[n].kind != NodeKind::Cup) continue;
    if (static_cast<int>(n) == cup_node) index = cups;
    ++cups;
  }
  if (index < 0) return {};
  return {index * kOuterPanes / cups, (index + 1) * kOuterPanes / cups};
}

// How many of a lantern's `panes` are fully lit with `count` of its `target` drops in it (a lantern
// with mud in it stays dark).
inline int PanesLit(int count, int target, int panes, bool impure) {
  if (impure || target <= 0) return 0;
  return std::min(panes, count * panes / target);
}

}  // namespace threadlock
