#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "threadlock_rules.h"

// Level files (assets/levels/world<N>.json) and the solver's pars (assets/levels/pars.json).
//
// A world file:
//   {"world": 1, "name": "First Light", "accent": "B", "levels": [ <level>, ... ]}
// A level:
//   {"id": "1-4", "name": "Two Cups", "type": "build", "intro": "...",
//    "nodes": [{"id": "s", "kind": "spout", "at": [180, 30], "beads": "B+A*6", "beat": 12, "start": 1},
//              {"id": "f", "kind": "fork", "at": [180, 120], "socket": true, "flap": 0, "tap": false},
//              {"id": "t", "kind": "pass", "at": [270, 220]},          (pass nodes are sockets)
//              {"id": "x", "kind": "cross", "at": [180, 340], "socket": true},
//              {"id": "c", "kind": "cup", "at": [270, 470], "want": "B", "count": 6}, ...],
//    "pipes": [["s", "f"], ["f", "x", [[90, 170], [90, 280]]], ...],   (from, to, bends)
//    "tray": ["separator", "turnstile", "tank4", {"type": "overflow", "n": 2}],
//    "placed": [{"piece": "turnstile", "at": "x", "set": 1, "locked": true}]}
// Machine parts (DESIGN.md §4):
//   {"id": "r", "kind": "rack", "at": [180, 470], "period": 4, "first": 0,
//    "slots": [{"want": "B", "count": 4, "order": "Mrs Hale"}, {"want": "B", "count": 2}]}
//   {"id": "m", "kind": "mill", "at": [90, 300], "gears": ["g"]}
//   {"id": "g", "kind": "gate", "at": [270, 200], "open": false}
// Look only (the rules ignore them): "height": 1200 = a grand machine taller than one screen (board units,
// at least 600 = one screen; it scrolls), "payoff": [180, 870] = where the payoff happens (World 1's rose
// window), "shelves": [150, 330] = World 2's shop wall (its pipes then run straight across or down), and a
// cup's or bottle's "order": "Mrs Hale" (the slip it fills).
// Sockets ("socket": true; pass nodes are sockets unless false) go on forks, passes, crossings and
// merges; any piece goes in any socket. A pipe leaving a fork is its side 0 or 1 in the order the
// pipes are listed.
// The pars file maps a level id to its pars: {"1-4": {"pieces": 2, "ticks": 131, "solutions": 4,
//   "lean": <placement>, "swift": <placement>}}, a placement being
//   {"at": ["f", "x", null], "set": [0, 1, 0], "flaps": {"f": 1}}.
namespace threadlock {

struct World {
  int number = 0;
  std::string name;
  Color accent = Color::Blue;
  std::vector<Level> levels;
};

// Parses and finalizes every level. False (and `errors`) if anything is wrong.
bool ParseWorld(std::string_view json, World& out, std::vector<std::string>& errors);
bool ParseLevel(std::string_view json, Level& out, std::vector<std::string>& errors);

// Fills in each level's pars from a pars file. Levels missing from it keep unknown pars.
bool ApplyPars(std::string_view json, std::vector<World>& worlds, std::vector<std::string>& errors);
// The pars file for these levels (sorted by id).
std::string WritePars(const std::vector<const Level*>& levels);

std::string PlacementText(const Level& level, const Placement& placement);  // one line, for logs and tools

// A level in the file format (one level object), e.g. to turn a generated machine into a handmade
// level's first draft.
std::string WriteLevel(const Level& level);

// What's wrong with a level's look (the rules don't care, the art does): a node off its board; on a shop
// wall (World 2, a level with shelves) a pipe that isn't straight across or straight down. Empty = good.
std::vector<std::string> LookProblems(const Level& level);

}  // namespace threadlock
