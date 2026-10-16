#pragma once

// Small levels for the rules and solver tests, in the level file format (level_io.h).

#include <string>
#include <vector>

#include "../src/level_io.h"
#include "doctest.h"

namespace threadlock {

inline Level MakeLevel(const std::string& json) {
  Level level;
  std::vector<std::string> errors;
  const bool ok = ParseLevel(json, level, errors);
  for (const std::string& error : errors) INFO(error);
  REQUIRE(ok);
  return level;
}

// DESIGN.md's level 1-4: a blend pours into a fork; the cups are swapped, so the sorted colours
// cross, and both lanes reach the crossing at the same tick (30 cells; amber's lane has a pipe
// socket in it).
inline const std::string kTwoCups = R"({
  "id": "t-two-cups", "name": "Two Cups",
  "nodes": [
    {"id": "s", "kind": "spout", "at": [180, 42], "beads": "B+A*6", "beat": 12},
    {"id": "f", "kind": "fork", "at": [180, 140], "socket": true},
    {"id": "t", "kind": "pass", "at": [270, 242]},
    {"id": "x", "kind": "cross", "at": [180, 360], "socket": true},
    {"id": "cb", "kind": "cup", "at": [270, 496], "want": "B", "count": 6},
    {"id": "ca", "kind": "cup", "at": [90, 496], "want": "A", "count": 6}
  ],
  "pipes": [["s", "f"], ["f", "x", [[90, 192], [90, 292]]], ["f", "t", [[270, 192]]], ["t", "x", [[270, 300]]],
            ["x", "cb", [[270, 428]]], ["x", "ca", [[90, 428]]]],
  "tray": ["separator", "turnstile", "tank4"]
})";

// Six blue drops, two forks, three cups of two (DESIGN.md's 2-5, Three Ways).
inline const std::string kTwoForks = R"({
  "id": "t-two-forks",
  "nodes": [
    {"id": "s", "kind": "spout", "at": [180, 40], "beads": "B*6", "beat": 10},
    {"id": "a", "kind": "fork", "at": [180, 140], "socket": true, "tap": true},
    {"id": "b", "kind": "fork", "at": [250, 270], "socket": true, "flap": 1},
    {"id": "c1", "kind": "cup", "at": [70, 480], "want": "B", "count": 2},
    {"id": "c2", "kind": "cup", "at": [190, 480], "want": "B", "count": 2},
    {"id": "c3", "kind": "cup", "at": [300, 480], "want": "B", "count": 2}
  ],
  "pipes": [["s", "a"], ["a", "c1", [[70, 200]]], ["a", "b", [[250, 200]]], ["b", "c2", [[190, 330]]],
            ["b", "c3", [[300, 330]]]],
  "tray": ["flipper", {"type": "overflow", "n": 2}]
})";

// Two spouts whose lanes cross on the same tick: the left spout's lane ends in the right cup.
inline std::string Crossing(const std::string& left, const std::string& right, const std::string& tray = "") {
  return R"({"id": "t-crossing", "nodes": [
    {"id": "sl", "kind": "spout", "at": [90, 40], "beads": ")" +
         left + R"(", "beat": 12},
    {"id": "sr", "kind": "spout", "at": [270, 40], "beads": ")" +
         right + R"(", "beat": 12},
    {"id": "x", "kind": "cross", "at": [180, 220], "socket": true},
    {"id": "cr", "kind": "cup", "at": [270, 420], "want": ")" +
         left.substr(0, 1) + R"(", "count": 4},
    {"id": "cl", "kind": "cup", "at": [90, 420], "want": ")" +
         right.substr(0, 1) + R"(", "count": 4}],
  "pipes": [["sl", "x", [[90, 120]]], ["sr", "x", [[270, 120]]], ["x", "cr", [[270, 320]]], ["x", "cl", [[90, 320]]]],
  "tray": [)" +
         tray + "]}";
}

// Two spouts into a merge, one cup below it.
inline std::string Merge(const std::string& left, const std::string& right, int count, const std::string& want,
                         const std::string& tray = "") {
  return R"({"id": "t-merge", "nodes": [
    {"id": "sl", "kind": "spout", "at": [90, 40], "beads": ")" +
         left + R"(", "beat": 12},
    {"id": "sr", "kind": "spout", "at": [270, 40], "beads": ")" +
         right + R"(", "beat": 12},
    {"id": "m", "kind": "merge", "at": [180, 220], "socket": true},
    {"id": "c", "kind": "cup", "at": [180, 420], "want": ")" +
         want + R"(", "count": )" + std::to_string(count) + R"(}],
  "pipes": [["sl", "m", [[90, 120]]], ["sr", "m", [[270, 120]]], ["m", "c"]],
  "tray": [)" +
         tray + "]}";
}

// Blue goes down through x1 then x2; amber goes up through x2 then x1, and the pipes between the
// two crossings are short (DESIGN.md's 3-8, Two Locks).
inline const std::string kTwoLocks = R"({
  "id": "t-two-locks",
  "nodes": [
    {"id": "sb", "kind": "spout", "at": [60, 40], "beads": "B*12", "beat": 2},
    {"id": "sa", "kind": "spout", "at": [300, 560], "beads": "A*12", "beat": 2},
    {"id": "x1", "kind": "cross", "at": [180, 250], "socket": true},
    {"id": "x2", "kind": "cross", "at": [180, 290], "socket": true},
    {"id": "cb", "kind": "cup", "at": [60, 560], "want": "B", "count": 12},
    {"id": "ca", "kind": "cup", "at": [300, 40], "want": "A", "count": 12}
  ],
  "pipes": [["sb", "x1", [[60, 150]]], ["x1", "x2", [[195, 270]]], ["x2", "cb", [[60, 440]]],
            ["sa", "x2", [[300, 430]]], ["x2", "x1", [[165, 270]]], ["x1", "ca", [[300, 150]]]],
  "tray": ["turnstile", {"type": "turnstile"}, "bridge"]
})";

// Two parallel pipes through a twin socket (p + q); the right spout starts 4 ticks later.
inline const std::string kTwins = R"({
  "id": "t-twins",
  "nodes": [
    {"id": "sp", "kind": "spout", "at": [100, 40], "beads": "B*4", "beat": 12},
    {"id": "sq", "kind": "spout", "at": [260, 40], "beads": "A*4", "beat": 12, "start": 5},
    {"id": "p", "kind": "pass", "at": [100, 200], "twin": "q"},
    {"id": "q", "kind": "pass", "at": [260, 200]},
    {"id": "cp", "kind": "cup", "at": [100, 450], "want": "B", "count": 4},
    {"id": "cq", "kind": "cup", "at": [260, 450], "want": "A", "count": 4}
  ],
  "pipes": [["sp", "p"], ["p", "cp"], ["sq", "q"], ["q", "cq"]],
  "tray": ["twin"]
})";

// Two Cups as a repair level: a locked separator, a movable tank already placed, a spare separator.
inline const std::string kRepair = R"({
  "id": "t-repair", "type": "repair",
  "nodes": [
    {"id": "s", "kind": "spout", "at": [180, 42], "beads": "B+A*6", "beat": 12},
    {"id": "f", "kind": "fork", "at": [180, 140], "socket": true},
    {"id": "t", "kind": "pass", "at": [270, 242]},
    {"id": "x", "kind": "cross", "at": [180, 360], "socket": true},
    {"id": "cb", "kind": "cup", "at": [270, 496], "want": "B", "count": 6},
    {"id": "ca", "kind": "cup", "at": [90, 496], "want": "A", "count": 6}
  ],
  "pipes": [["s", "f"], ["f", "x", [[90, 192], [90, 292]]], ["f", "t", [[270, 192]]], ["t", "x", [[270, 300]]],
            ["x", "cb", [[270, 428]]], ["x", "ca", [[90, 428]]]],
  "tray": ["separator"],
  "placed": [{"piece": "separator", "at": "f", "locked": true}, {"piece": "tank4", "at": "t"}]
})";

}  // namespace threadlock
