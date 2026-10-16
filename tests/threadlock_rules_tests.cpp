// Tests for Threadlock's rules v2 (threadlock_rules.h): every piece, every way to fail, the tray and
// sockets, determinism. Levels are written as JSON, the same format as assets/levels.
#include <string>
#include <utility>
#include <vector>

#include "doctest.h"
#include "test_levels.h"

using namespace threadlock;

namespace {

struct Run {
  Outcome outcome;
  std::vector<std::pair<int, Event>> timed;  // (tick, event)

  int Count(EventType type) const {
    int n = 0;
    for (const auto& [tick, e] : timed) n += e.type == type;
    return n;
  }
  int FirstTick(EventType type, int node, Color color = Color::None) const {
    for (const auto& [tick, e] : timed) {
      if (e.type == type && e.node == node && (color == Color::None || e.bead.a == color)) return tick;
    }
    return -1;
  }
};

Run RunBoard(Board& board) {
  Run run;
  board.Start();
  while (board.Running()) {
    for (const Event& e : board.Tick()) run.timed.push_back({board.Ticks(), e});
  }
  run.outcome = board.GetOutcome();
  return run;
}

int NodeOf(const Level& level, const char* id) {
  const int n = level.NodeIndex(id);
  REQUIRE(n >= 0);
  return n;
}

int PieceOf(const Board& board, PieceType type, int nth = 0) {
  for (int i = 0; i < board.PieceCount(); ++i) {
    if (board.PieceInfo(i).piece.type == type && nth-- == 0) return i;
  }
  FAIL("no such piece in the tray");
  return -1;
}

}  // namespace

TEST_SUITE("rules") {
  TEST_CASE("beads parse, print and mix") {
    std::vector<Bead> beads;
    REQUIRE(ParseBeads("B+A*3 B A", beads));
    CHECK(beads.size() == 5);
    CHECK(beads[0] == Bead::Blend(Color::Amber, Color::Blue));  // blends are kept in one order
    CHECK(beads[0].Drops() == 2);
    CHECK(BeadsText(beads) == "B+A*3 B A");
    CHECK(!ParseBeads("B*0", beads));
    CHECK(!ParseBeads("X", beads));
    CHECK(Mix(Color::Blue, Color::Amber) == Color::Green);
    CHECK(Mix(Color::Rose, Color::Green) == Color::Pearl);
    CHECK(Mix(Color::Blue, Color::Blue) == Color::Blue);
    CHECK(Mix(Color::Blue, Color::Rose) == Color::Mud);
  }

  TEST_CASE("cells follow the drawn length") {
    const Level level = MakeLevel(kTwoCups);
    CHECK(level.pipes[0].cells == 7);   // spout (r 22) to fork (r 5), 98 apart: 71 drawn
    CHECK(level.pipes[1].cells == 30);  // the blue lane
    CHECK(level.pipes[2].cells == 14);  // amber: 14 + the pass socket's tick + 15 = 30 too
    CHECK(level.pipes[3].cells == 15);  // the amber bend sits a little lower to make it 15
    CHECK(level.nodes[static_cast<size_t>(NodeOf(level, "x"))].dwell == 3);
  }

  TEST_CASE("an empty fork follows its flap: the blend lands whole, in the wrong cup") {
    const Level level = MakeLevel(kTwoCups);
    Board board(level);
    const Run run = RunBoard(board);
    CHECK(run.outcome.over);
    CHECK(!run.outcome.solved);
    CHECK(run.outcome.failure == Failure::WrongCup);
    CHECK(run.Count(EventType::Collide) == 0);
    CHECK(board.Cup(NodeOf(level, "cb")).count == 12);  // nothing is lost: 6 blends = 12 drops
    CHECK(board.Cup(NodeOf(level, "ca")).count == 0);
  }

  TEST_CASE("Two Cups: a separator alone makes mud at the crossing") {
    const Level level = MakeLevel(kTwoCups);
    Board board(level);
    REQUIRE(board.Place(PieceOf(board, PieceType::Separator), NodeOf(level, "f")));
    const Run run = RunBoard(board);
    CHECK(!run.outcome.solved);
    CHECK(run.outcome.failure == Failure::Mud);
    CHECK(run.outcome.fail_node == NodeOf(level, "x"));
    CHECK(run.outcome.fail_tick == 39);  // both halves enter on tick 39
    CHECK(run.Count(EventType::Collide) == 6);
  }

  TEST_CASE("Two Cups: separator + turnstile, amber waits 3 ticks") {
    const Level level = MakeLevel(kTwoCups);
    Board board(level);
    board.Place(PieceOf(board, PieceType::Separator), NodeOf(level, "f"));
    board.Place(PieceOf(board, PieceType::Turnstile), NodeOf(level, "x"));
    const Run run = RunBoard(board);
    CHECK(run.outcome.solved);
    CHECK(run.outcome.pieces == 2);
    // Blue (lane 0, the arrow's side) enters at 39; amber enters at 42, the tick blue leaves.
    CHECK(run.FirstTick(EventType::Enter, NodeOf(level, "x"), Color::Blue) == 39);
    CHECK(run.FirstTick(EventType::Enter, NodeOf(level, "x"), Color::Amber) == 42);
    CHECK(run.Count(EventType::Wait) >= 6);
  }

  TEST_CASE("Two Cups: the turnstile's arrow picks who goes first") {
    const Level level = MakeLevel(kTwoCups);
    Board board(level);
    board.Place(PieceOf(board, PieceType::Separator), NodeOf(level, "f"));
    const int turnstile = PieceOf(board, PieceType::Turnstile);
    board.Place(turnstile, NodeOf(level, "x"));
    board.SetSetting(turnstile, 1);
    const Run run = RunBoard(board);
    CHECK(run.outcome.solved);
    CHECK(run.FirstTick(EventType::Enter, NodeOf(level, "x"), Color::Amber) == 39);
    CHECK(run.FirstTick(EventType::Enter, NodeOf(level, "x"), Color::Blue) == 42);
  }

  TEST_CASE("Two Cups: separator + short tank solves one tick slower, with near misses") {
    const Level level = MakeLevel(kTwoCups);
    Board turnstile_board(level), tank_board(level);
    turnstile_board.Place(PieceOf(turnstile_board, PieceType::Separator), NodeOf(level, "f"));
    turnstile_board.Place(PieceOf(turnstile_board, PieceType::Turnstile), NodeOf(level, "x"));
    tank_board.Place(PieceOf(tank_board, PieceType::Separator), NodeOf(level, "f"));
    tank_board.Place(PieceOf(tank_board, PieceType::TankShort), NodeOf(level, "t"));
    const Run with_turnstile = RunBoard(turnstile_board);
    const Run with_tank = RunBoard(tank_board);
    CHECK(with_tank.outcome.solved);
    CHECK(with_tank.outcome.ticks == with_turnstile.outcome.ticks + 1);
    CHECK(with_tank.Count(EventType::NearMiss) == 6);       // amber enters the tick after blue leaves
    CHECK(with_turnstile.Count(EventType::NearMiss) == 0);  // a lock isn't a near miss
  }

  TEST_CASE("sockets have shapes") {
    const Level level = MakeLevel(kTwoCups);
    Board board(level);
    const int tank = PieceOf(board, PieceType::TankShort);
    CHECK(!board.CanPlace(tank, NodeOf(level, "x")));  // pipe sockets only
    CHECK(!board.CanPlace(tank, NodeOf(level, "f")));
    CHECK(!board.CanPlace(PieceOf(board, PieceType::Separator), NodeOf(level, "x")));
    CHECK(!board.CanPlace(PieceOf(board, PieceType::Turnstile), NodeOf(level, "t")));
    CHECK(!board.CanPlace(tank, NodeOf(level, "s")));  // not a socket
    CHECK(board.CanPlace(tank, NodeOf(level, "t")));
  }

  TEST_CASE("placing: one piece per socket, displaced pieces go back to the tray") {
    const Level level = MakeLevel(kTwoForks);
    Board board(level);
    const int flipper = PieceOf(board, PieceType::Flipper);
    const int overflow = PieceOf(board, PieceType::Overflow);
    REQUIRE(board.Place(flipper, NodeOf(level, "a")));
    CHECK(board.PieceAt(NodeOf(level, "a")) == flipper);
    REQUIRE(board.Place(flipper, NodeOf(level, "b")));  // it moves
    CHECK(board.PieceAt(NodeOf(level, "a")) == -1);
    REQUIRE(board.Place(overflow, NodeOf(level, "b")));  // takes the socket: the flipper goes back
    CHECK(board.PieceNode(flipper) == -1);
    CHECK(board.PlacedCount() == 1);
    board.Unplace(overflow);
    CHECK(board.PlacedCount() == 0);
  }

  TEST_CASE("the same colour may share a junction; different colours make mud") {
    {
      const Level level = MakeLevel(Crossing("B*4", "B*4"));
      Board board(level);
      const Run run = RunBoard(board);
      CHECK(run.Count(EventType::Collide) == 0);
      CHECK(run.outcome.solved);
    }
    {
      const Level level = MakeLevel(Crossing("B*4", "A*4"));
      Board board(level);
      const Run run = RunBoard(board);
      CHECK(run.Count(EventType::Collide) == 4);
      CHECK(run.outcome.failure == Failure::Mud);
    }
  }

  TEST_CASE("a bridge keeps the lanes apart: no mud, no waiting") {
    const Level level = MakeLevel(Crossing("B*4", "A*4", "\"bridge\""));
    Board board(level);
    board.Place(PieceOf(board, PieceType::Bridge), NodeOf(level, "x"));
    const Run run = RunBoard(board);
    CHECK(run.outcome.solved);
    CHECK(run.Count(EventType::Wait) == 0);
  }

  TEST_CASE("a merge: same colours queue, different ones collide") {
    {
      const Level level = MakeLevel(Merge("B*3", "B*3", 6, "B"));
      Board board(level);
      const Run run = RunBoard(board);
      CHECK(run.outcome.solved);
      CHECK(run.Count(EventType::Collide) == 0);
      CHECK(board.Cup(NodeOf(level, "c")).count == 6);
    }
    {
      const Level level = MakeLevel(Merge("B*3", "A*3", 6, "B"));
      Board board(level);
      const Run run = RunBoard(board);
      CHECK(run.outcome.failure == Failure::Mud);
    }
  }

  TEST_CASE("a mixer makes two green from a blue and an amber") {
    const Level level = MakeLevel(Merge("B*3", "A*3", 6, "G", "\"mixer\""));
    Board board(level);
    board.Place(PieceOf(board, PieceType::Mixer), NodeOf(level, "m"));
    const Run run = RunBoard(board);
    CHECK(run.outcome.solved);
    CHECK(run.Count(EventType::Mix) == 3);
    CHECK(board.Cup(NodeOf(level, "c")).count == 6);
  }

  TEST_CASE("a mixer whose partner never comes: THREADLOCKED") {
    const Level level = MakeLevel(Merge("B*4", "A*3", 6, "G", "\"mixer\""));
    Board board(level);
    board.Place(PieceOf(board, PieceType::Mixer), NodeOf(level, "m"));
    const Run run = RunBoard(board);
    CHECK(run.outcome.jammed);
    CHECK(!run.outcome.solved);
    CHECK(run.outcome.failure == Failure::Jam);
    CHECK(run.Count(EventType::Jammed) == 1);
    CHECK(board.Jam().lonely == NodeOf(level, "m"));
    CHECK(board.Cup(NodeOf(level, "c")).count == 6);
  }

  TEST_CASE("flipper alternates, overflow counts") {
    const Level level = MakeLevel(kTwoForks);
    Board board(level);
    board.Place(PieceOf(board, PieceType::Overflow), NodeOf(level, "a"));  // 2 to the left cup...
    board.Place(PieceOf(board, PieceType::Flipper), NodeOf(level, "b"));   // ...the other 4 split 2 / 2
    const Run run = RunBoard(board);
    CHECK(run.outcome.solved);
    CHECK(board.Cup(NodeOf(level, "c1")).count == 2);
    CHECK(board.Cup(NodeOf(level, "c2")).count == 2);
    CHECK(board.Cup(NodeOf(level, "c3")).count == 2);
  }

  TEST_CASE("halving can't make thirds: spill") {
    const Level level = MakeLevel(kTwoForks);
    Board board(level);
    board.Place(PieceOf(board, PieceType::Flipper), NodeOf(level, "a"));
    board.Place(PieceOf(board, PieceType::Overflow), NodeOf(level, "b"));
    const Run run = RunBoard(board);
    CHECK(!run.outcome.solved);
    CHECK(board.Cup(NodeOf(level, "c1")).count == 3);
    CHECK(board.Cup(NodeOf(level, "c1")).spilled);
    CHECK(board.Cup(NodeOf(level, "c3")).count == 1);
    CHECK(run.outcome.failure == Failure::Spill);
    CHECK(run.Count(EventType::Spill) == 1);
  }

  TEST_CASE("a dry cup") {
    const Level level = MakeLevel(Merge("B*2", "B*2", 6, "B"));
    Board board(level);
    const Run run = RunBoard(board);
    CHECK(run.outcome.failure == Failure::Dry);
    CHECK(run.outcome.fail_node == NodeOf(level, "c"));
  }

  TEST_CASE("a tappable flap") {
    const Level level = MakeLevel(kTwoForks);
    Board board(level);
    CHECK(board.FlapTappable(NodeOf(level, "a")));
    CHECK(!board.FlapTappable(NodeOf(level, "b")));
    board.SetFlap(NodeOf(level, "a"), 1);
    CHECK(board.Flap(NodeOf(level, "a")) == 1);
    board.SetFlap(NodeOf(level, "b"), 0);  // no handle: stays
    CHECK(board.Flap(NodeOf(level, "b")) == 1);
    board.Place(PieceOf(board, PieceType::Flipper), NodeOf(level, "a"));
    CHECK(!board.FlapTappable(NodeOf(level, "a")));  // a piece covers it
  }

  TEST_CASE("two turnstiles taken in opposite orders: a circle of waiting") {
    const Level level = MakeLevel(kTwoLocks);
    Board board(level);
    board.Place(PieceOf(board, PieceType::Turnstile, 0), NodeOf(level, "x1"));
    const int second = PieceOf(board, PieceType::Turnstile, 1);
    board.Place(second, NodeOf(level, "x2"));
    board.SetSetting(second, 1);  // x2 lets amber go first, x1 blue: opposite lock orders
    const Run run = RunBoard(board);
    CHECK(run.outcome.jammed);
    const JamReport jam = board.Jam();
    CHECK(jam.cycle.size() == 2);
    CHECK(jam.lonely == -1);
  }

  TEST_CASE("bridge one crossing, lock the other: no circle") {
    const Level level = MakeLevel(kTwoLocks);
    Board board(level);
    board.Place(PieceOf(board, PieceType::Turnstile, 0), NodeOf(level, "x1"));
    board.Place(PieceOf(board, PieceType::Bridge), NodeOf(level, "x2"));
    const Run run = RunBoard(board);
    CHECK(run.outcome.solved);
  }

  TEST_CASE("a twin gate lets drops go in pairs") {
    const Level level = MakeLevel(kTwins);
    const int p = NodeOf(level, "p"), q = NodeOf(level, "q");
    Board board(level);
    {
      const Run run = RunBoard(board);
      CHECK(run.outcome.solved);
      CHECK(run.FirstTick(EventType::Leave, q) == run.FirstTick(EventType::Leave, p) + 4);
    }
    board.Stop();
    const int gate = PieceOf(board, PieceType::TwinGate);
    REQUIRE(board.Place(gate, p));
    CHECK(board.PieceAt(q) == gate);  // it covers both halves
    const Run run = RunBoard(board);
    CHECK(run.outcome.solved);
    CHECK(run.FirstTick(EventType::Leave, q) == run.FirstTick(EventType::Leave, p));
  }

  TEST_CASE("locked and preplaced pieces") {
    const Level level = MakeLevel(kRepair);
    Board board(level);
    // 0 = the tray's separator, 1 = the locked separator, 2 = the preplaced tank
    CHECK(board.PieceNode(1) == NodeOf(level, "f"));
    CHECK(board.Locked(1));
    CHECK(!board.CanPlace(0, NodeOf(level, "f")));  // a locked piece keeps its socket
    board.Unplace(1);
    CHECK(board.PieceNode(1) == NodeOf(level, "f"));
    CHECK(board.PieceNode(2) == NodeOf(level, "t"));
    CHECK(board.PlacedCount() == 1);  // locked pieces don't count for Lean
    board.Unplace(2);
    CHECK(board.PlacedCount() == 0);
    board.ClearPieces();
    CHECK(board.PieceNode(2) == NodeOf(level, "t"));  // back where the level put it
  }

  TEST_CASE("runs are deterministic and Start resets") {
    const Level level = MakeLevel(kTwoCups);
    Board a(level), b(level);
    a.Place(PieceOf(a, PieceType::Separator), NodeOf(level, "f"));
    b.Place(PieceOf(b, PieceType::Separator), NodeOf(level, "f"));
    const Run ra = RunBoard(a), rb = RunBoard(b);
    REQUIRE(ra.timed.size() == rb.timed.size());
    bool same = true;
    for (size_t i = 0; i < ra.timed.size(); ++i) {
      const Event& x = ra.timed[i].second;
      const Event& y = rb.timed[i].second;
      same &= ra.timed[i].first == rb.timed[i].first && x.type == y.type && x.drop == y.drop && x.node == y.node &&
              x.bead == y.bead;
    }
    CHECK(same);
    a.Stop();
    const Run again = RunBoard(a);
    CHECK(again.outcome.ticks == ra.outcome.ticks);
    CHECK(again.timed.size() == ra.timed.size());
  }

  TEST_CASE("pieces can't move while running") {
    const Level level = MakeLevel(kTwoCups);
    Board board(level);
    board.Start();
    CHECK(!board.Place(0, NodeOf(level, "f")));
    board.Stop();
    CHECK(board.Place(0, NodeOf(level, "f")));
  }

  TEST_CASE("bad levels are reported") {
    std::vector<std::string> errors;
    Level level;
    CHECK(
        !ParseLevel(R"({"id": "x", "nodes": [{"id": "s", "kind": "spout", "at": [0, 0], "beads": "B"}], "pipes": []})",
                    level, errors));
    CHECK(!errors.empty());
    errors.clear();
    CHECK(!ParseLevel("not json", level, errors));
    errors.clear();
    CHECK(!ParseLevel(R"({"id": "x", "nodes": [{"id": "s", "kind": "spout", "at": [0, 0], "beads": "Q"}]})", level,
                      errors));
  }
}
