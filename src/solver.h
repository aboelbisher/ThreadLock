#pragma once

#include <functional>
#include <vector>

#include "threadlock_rules.h"

// The solver: tries every placement of the tray's pieces (every socket where each piece acts, every
// setting, every tappable flap that still decides something) and runs each one. Runs are
// deterministic, so one run per placement says all there is to know. It gives the pars for the
// stars (Lean = fewest pieces, Swift = fastest run), the solutions for hints, and numbers for
// judging a level (how many solutions, traps).
//
// Sockets are open, but a piece sitting idle changes nothing and a trap can only fail, so neither
// can beat the same placement without that piece: the search leaves them out (and `solutions`
// counts only placements where every piece works). Identical pieces aren't tried in both orders,
// and a run stops at its first failure.
namespace threadlock {

struct Solution {
  Placement placement;
  int pieces = 0;
  int ticks = 0;
};

struct SolveReport {
  int placements = 0;  // tried
  int solutions = 0;
  Pars pars;
  bool two_answers = false;    // no single solution is both Lean and Swift
  std::vector<Solution> kept;  // every solution, if asked for (SolveOptions::keep)
};

struct SolveOptions {
  bool keep = false;               // keep every solution in the report
  int max_placements = 3'000'000;  // give up after this many (reported as placements == max)
};

// Runs one placement to its end (or its first failure).
Outcome Simulate(const Level& level, const Placement& placement, bool stop_at_failure = true);

// Calls `visit` for every distinct placement where each piece acts (the level's locked pieces stay
// where they are). Return false from `visit` to stop.
void ForEachPlacement(const Level& level, const std::function<bool(const Placement&)>& visit);

SolveReport Solve(const Level& level, const SolveOptions& options = {});

// A hint: the next piece to put down from a solution close to what's on the board now. Returns
// the tray piece, its node and setting, or piece = -1 if the board already matches the solution
// (or there's none).
struct HintMove {
  int piece = -1;
  int node = -1;
  int setting = 0;
  int flap_node = -1;  // or a flap to set (piece = -1)
  int flap = 0;
};
HintMove NextHint(const Level& level, const Placement& current, const Placement& solution);

}  // namespace threadlock
