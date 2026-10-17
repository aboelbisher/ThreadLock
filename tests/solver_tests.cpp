// Tests for the solver (solver.h): pars, every placement once, hints.
#include <set>
#include <string>
#include <vector>

#include "../src/solver.h"
#include "doctest.h"
#include "test_levels.h"

using namespace threadlock;

TEST_SUITE("solver") {
  TEST_CASE("Two Cups: one separator on the crossing is both Lean and Swift") {
    const Level level = MakeLevel(kTwoCups);
    const SolveReport report = Solve(level, {.keep = true});
    REQUIRE(report.solutions > 0);
    CHECK(report.pars.pieces == 1);
    CHECK(report.pars.ticks == 119);
    const int x = level.NodeIndex("x"), f = level.NodeIndex("f");
    CHECK(report.pars.lean.at == std::vector<int>{x, -1, -1});  // the separator sorts the blends there
    CHECK(!report.two_answers);
    // Every solution has the separator on the fork or the crossing, and nothing idle.
    for (const Solution& solution : report.kept) {
      CHECK((solution.placement.at[0] == f || solution.placement.at[0] == x));
      Board board(level);
      board.SetPlacement(solution.placement);
      for (int piece = 0; piece < board.PieceCount(); ++piece) CHECK(board.RoleOf(piece) == PieceRole::Acts);
    }
  }

  TEST_CASE("every placement is visited once") {
    const Level level = MakeLevel(kTwoLocks);  // two identical turnstiles: not tried in both orders
    std::set<std::vector<int>> seen;
    int visits = 0;
    ForEachPlacement(level, [&](const Placement& placement) {
      ++visits;
      std::vector<int> key = placement.at;
      key.insert(key.end(), placement.setting.begin(), placement.setting.end());
      seen.insert(key);
      return true;
    });
    CHECK(visits == static_cast<int>(seen.size()));
    // turnstiles: {none, x1, x2} as a multiset: none+none, none+x1 (2 settings), none+x2 (2), x1+x2 (4)
    // = 9; the bridge: tray, x1, x2 minus what the turnstiles took. (Both pieces act on crossings.)
    CHECK(visits == 1 * 3 + 2 * 2 + 2 * 2 + 4 * 1);
  }

  TEST_CASE("Three Ways: overflow first, flipper second") {
    const Level level = MakeLevel(kTwoForks);
    const SolveReport report = Solve(level, {.keep = true});
    REQUIRE(report.solutions >= 1);
    CHECK(report.pars.pieces == 2);
    for (const Solution& solution : report.kept) {
      CHECK(solution.placement.at[1] == level.NodeIndex("a"));  // the overflow
    }
  }

  TEST_CASE("a hint moves the next piece of the solution into place") {
    const Level level = MakeLevel(kTwoCups);
    const SolveReport report = Solve(level);
    Board board(level);
    for (int step = 0; step < 4; ++step) {
      const HintMove move = NextHint(level, board.GetPlacement(), report.pars.lean);
      if (move.piece < 0 && move.flap_node < 0) break;
      if (move.piece >= 0 && move.node >= 0) {
        REQUIRE(board.Place(move.piece, move.node));
        board.SetSetting(move.piece, move.setting);
      } else if (move.piece >= 0) {
        board.Unplace(move.piece);
      }
    }
    board.Start();
    while (board.Running()) board.Tick();
    CHECK(board.GetOutcome().solved);
  }

  TEST_CASE("a hint takes away a piece the solution doesn't use") {
    const Level level = MakeLevel(kTwoCups);
    const SolveReport report = Solve(level);
    Board board(level);
    board.SetPlacement(report.pars.lean);
    const int extra = report.pars.lean.at[2] < 0 ? 2 : 1;  // the tank or the turnstile, whichever is unused
    REQUIRE(board.Place(extra, extra == 2 ? level.NodeIndex("t") : level.NodeIndex("x")));
    const HintMove move = NextHint(level, board.GetPlacement(), report.pars.lean);
    CHECK(move.piece == extra);
    CHECK(move.node == -1);
  }
}
