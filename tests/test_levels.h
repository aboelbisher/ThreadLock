#pragma once

// Small levels for the rules and solver tests, in the level file format (level_io.h).

#include <string>
#include <utility>
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

// A run's outcome and every event with the tick it happened on.
struct Run {
  Outcome outcome;
  std::vector<std::pair<int, Event>> timed;  // (tick, event)

  int Count(EventType type, int node = -1) const {
    int n = 0;
    for (const auto& [tick, e] : timed) n += e.type == type && (node < 0 || e.node == node);
    return n;
  }
  int FirstTick(EventType type, int node, Color color = Color::None) const {
    for (const auto& [tick, e] : timed) {
      if (e.type == type && e.node == node && (color == Color::None || e.bead.a == color)) return tick;
    }
    return -1;
  }
  std::vector<int> Ticks(EventType type, int node) const {  // every tick it happened at `node`
    std::vector<int> ticks;
    for (const auto& [tick, e] : timed) {
      if (e.type == type && e.node == node) ticks.push_back(tick);
    }
    return ticks;
  }
};

inline Run RunBoard(Board& board) {
  Run run;
  board.Start();
  while (board.Running()) {
    for (const Event& e : board.Tick()) run.timed.push_back({board.Ticks(), e});
  }
  run.outcome = board.GetOutcome();
  return run;
}

inline int NodeOf(const Level& level, const char* id) {
  const int n = level.NodeIndex(id);
  REQUIRE(n >= 0);
  return n;
}

inline int PieceOf(const Board& board, PieceType type, int nth = 0) {
  for (int i = 0; i < board.PieceCount(); ++i) {
    if (board.PieceInfo(i).piece.type == type && nth-- == 0) return i;
  }
  FAIL("no such piece in the tray");
  return -1;
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

// Two spouts into a crossing, as in Crossing() but with any cups and timing: the left lane (0) goes
// straight on to the right cup `cr`, the right lane (1) to the left cup `cl`. Both lanes are 18 cells,
// so the right spout starting `right_start - 1` ticks later arrives that much later.
inline std::string CrossLevel(const std::string& left, const std::string& right, int right_start,
                              const std::string& cr_want, int cr_count, const std::string& cl_want, int cl_count,
                              const std::string& tray = "", int beat = 12) {
  return R"({"id": "t-cross", "nodes": [
    {"id": "sl", "kind": "spout", "at": [90, 40], "beads": ")" +
         left + R"(", "beat": )" + std::to_string(beat) + R"(},
    {"id": "sr", "kind": "spout", "at": [270, 40], "beads": ")" +
         right + R"(", "beat": )" + std::to_string(beat) + R"(, "start": )" + std::to_string(right_start) + R"(},
    {"id": "x", "kind": "cross", "at": [180, 220], "socket": true},
    {"id": "cr", "kind": "cup", "at": [270, 420], "want": ")" +
         cr_want + R"(", "count": )" + std::to_string(cr_count) + R"(},
    {"id": "cl", "kind": "cup", "at": [90, 420], "want": ")" +
         cl_want + R"(", "count": )" + std::to_string(cl_count) + R"(}],
  "pipes": [["sl", "x", [[90, 120]]], ["sr", "x", [[270, 120]]], ["x", "cr", [[270, 320]]], ["x", "cl", [[90, 320]]]],
  "tray": [)" +
         tray + "]}";
}

// One spout into a fork with a handled flap (side 0 = the cup, side 1 = a drain).
inline std::string FlapFork(const std::string& tray) {
  return R"({"id": "t-flap-fork", "nodes": [
    {"id": "s", "kind": "spout", "at": [180, 40], "beads": "B*3", "beat": 12},
    {"id": "f", "kind": "fork", "at": [180, 200], "socket": true, "tap": true},
    {"id": "c", "kind": "cup", "at": [90, 420], "want": "B", "count": 3},
    {"id": "d", "kind": "drain", "at": [270, 420]}],
  "pipes": [["s", "f"], ["f", "c", [[90, 300]]], ["f", "d", [[270, 300]]]],
  "tray": [)" +
         tray + "]}";
}

// One spout through a single pipe socket into a cup.
inline std::string PipeSocket(const std::string& tray) {
  return R"({"id": "t-pipe-socket", "nodes": [
    {"id": "s", "kind": "spout", "at": [180, 40], "beads": "B*3", "beat": 12},
    {"id": "p", "kind": "pass", "at": [180, 200]},
    {"id": "c", "kind": "cup", "at": [180, 420], "want": "B", "count": 3}],
  "pipes": [["s", "p"], ["p", "c"]],
  "tray": [)" +
         tray + "]}";
}

// Six blue drops, one every 4 ticks, through a pipe socket into a turning rack of three bottles that
// turns every 8 ticks: with nothing placed they land at 25, 29, ..., 45, two in each bottle.
inline const std::string kRack = R"({
  "id": "t-rack",
  "nodes": [
    {"id": "s", "kind": "spout", "at": [180, 40], "beads": "B*6", "beat": 4},
    {"id": "t", "kind": "pass", "at": [180, 140]},
    {"id": "r", "kind": "rack", "at": [180, 300], "period": 8,
     "slots": [{"want": "B", "count": 2, "order": "Mrs Hale"}, {"want": "B", "count": 2, "order": "Baker"},
               {"want": "B", "count": 2, "order": "Dr Moss"}]}
  ],
  "pipes": [["s", "t"], ["t", "r"]],
  "tray": ["tank4", "tank8"]
})";

// The amber stream turns a mill geared to a shut gate on the blue stream: the first amber drop opens
// it, the second shuts it again.
inline const std::string kMillGate = R"({
  "id": "t-mill-gate",
  "nodes": [
    {"id": "sa", "kind": "spout", "at": [90, 40], "beads": "A*2", "beat": 12},
    {"id": "m", "kind": "mill", "at": [90, 250], "gears": ["g"]},
    {"id": "ca", "kind": "cup", "at": [90, 480], "want": "A", "count": 2},
    {"id": "sb", "kind": "spout", "at": [270, 40], "beads": "B", "beat": 12},
    {"id": "g", "kind": "gate", "at": [270, 150], "open": false},
    {"id": "cb", "kind": "cup", "at": [270, 480], "want": "B", "count": 1}
  ],
  "pipes": [["sa", "m"], ["m", "ca"], ["sb", "g"], ["g", "cb"]],
  "tray": []
})";

// DESIGN.md's Two Mills: each stream's mill flips the other stream's gate.
inline std::string TwoMills(bool a_open) {
  return R"({"id": "t-two-mills", "nodes": [
    {"id": "sa", "kind": "spout", "at": [90, 40], "beads": "A*2", "beat": 12},
    {"id": "ga", "kind": "gate", "at": [90, 140], "open": )" +
         std::string(a_open ? "true" : "false") + R"(},
    {"id": "ma", "kind": "mill", "at": [90, 280], "gears": ["gb"]},
    {"id": "ca", "kind": "cup", "at": [90, 480], "want": "A", "count": 2},
    {"id": "sb", "kind": "spout", "at": [270, 40], "beads": "B*2", "beat": 12},
    {"id": "gb", "kind": "gate", "at": [270, 140], "open": false},
    {"id": "mb", "kind": "mill", "at": [270, 280], "gears": ["ga"]},
    {"id": "cb", "kind": "cup", "at": [270, 480], "want": "B", "count": 2}],
  "pipes": [["sa", "ga"], ["ga", "ma"], ["ma", "ca"], ["sb", "gb"], ["gb", "mb"], ["mb", "cb"]],
  "tray": []})";
}

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

// World 3's 3-6 The Mill Gate, a canal map: each amber barge turns the mill, which flips the blue gate (its rod
// runs along Mill Lane, bending at (250, 360)); the harbours serve Lantern Row and Old Quay.
inline const std::string kMillGateCanals = R"({
  "id": "t-3-6", "name": "The Mill Gate",
  "nodes": [
    {"id": "sb", "kind": "spout", "at": [60, 30], "beads": "B*6", "beat": 3, "start": 12},
    {"id": "sa", "kind": "spout", "at": [300, 30], "beads": "A*6", "beat": 6},
    {"id": "pb", "kind": "pass", "at": [60, 150]},
    {"id": "pa", "kind": "pass", "at": [240, 120]},
    {"id": "x", "kind": "cross", "at": [180, 270], "socket": true},
    {"id": "g", "kind": "gate", "at": [250, 270], "open": false},
    {"id": "m", "kind": "mill", "at": [180, 360], "gears": [["g", [[250, 360]]]]},
    {"id": "ca", "kind": "cup", "at": [60, 520], "want": "A", "count": 6, "district": "Lantern Row"},
    {"id": "cb", "kind": "cup", "at": [300, 520], "want": "B", "count": 6, "district": "Old Quay"}
  ],
  "pipes": [["sb", "pb"], ["pb", "x", [[60, 270]]], ["sa", "pa", [[300, 120]]], ["pa", "x", [[180, 120]]],
            ["x", "g"], ["g", "cb", [[300, 270]]], ["x", "m"], ["m", "ca", [[180, 420], [60, 420]]]],
  "tray": ["turnstile", "tank4"]
})";

}  // namespace threadlock
