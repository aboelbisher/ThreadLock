// Tests for open sockets (DESIGN.md §3, "Every piece in every socket"): what each piece does in each
// kind of socket, the exact mud and flap rules, idle pieces and traps.
#include <string>
#include <vector>

#include "../src/solver.h"
#include "doctest.h"
#include "test_levels.h"

using namespace threadlock;

TEST_SUITE("sockets") {
  TEST_CASE("the piece x socket table") {
    // Fork, single pipe socket, twin pair, crossing, merge: a = acts, - = idle, T = trap.
    const char* const table[kPieceTypes] = {
        "a--a-",  // separator
        "---aa",  // turnstile
        "aaaaa",  // short tank
        "aaaaa",  // tall tank
        "a--a-",  // flipper
        "a--a-",  // overflow
        "---a-",  // bridge
        "TTTaa",  // mixer
        "TTaaa",  // twin gate
    };
    const NodeKind kinds[] = {NodeKind::Fork, NodeKind::Pass, NodeKind::Pass, NodeKind::Cross, NodeKind::Merge};
    for (int type = 0; type < kPieceTypes; ++type) {
      for (int k = 0; k < 5; ++k) {
        const PieceRole role = Role(static_cast<PieceType>(type), kinds[k], k == 2);
        const char want = table[type][k];
        INFO(PieceKey(static_cast<PieceType>(type)) << " in socket " << k);
        CHECK(role == (want == 'a' ? PieceRole::Acts : want == 'T' ? PieceRole::Trap : PieceRole::Idle));
      }
    }
    CHECK(Role(PieceType::TankShort, NodeKind::Cup) == PieceRole::Idle);  // not a socket at all
  }

  TEST_CASE("a separator on a crossing sorts by colour whatever the lane") {
    // The left lane pours B A B A; much later the right lane pours one B, which crosses over to the B cup.
    const Level level = MakeLevel(CrossLevel("B A B A", "B", 200, "B", 3, "A", 2, "\"separator\"", 16));
    Board board(level);
    CHECK(!RunBoard(board).outcome.solved);  // lanes go straight on: amber reaches the blue cup
    board.Stop();
    REQUIRE(board.Place(PieceOf(board, PieceType::Separator), NodeOf(level, "x")));
    CHECK(board.RoleOf(0) == PieceRole::Acts);
    const Run run = RunBoard(board);
    CHECK(run.outcome.solved);
    CHECK(board.Cup(NodeOf(level, "cr")).count == 3);
    CHECK(board.Cup(NodeOf(level, "cl")).count == 2);
  }

  TEST_CASE("Two Cups: a lone separator on the crossing splits the blends there") {
    const Level level = MakeLevel(kTwoCups);
    Board board(level);
    REQUIRE(board.Place(PieceOf(board, PieceType::Separator), NodeOf(level, "x")));
    const Run run = RunBoard(board);
    CHECK(run.outcome.solved);
    CHECK(run.Count(EventType::Split, NodeOf(level, "x")) == 6);
    CHECK(run.Count(EventType::Collide) == 0);  // a blend is one bead: it can't make mud with itself
  }

  TEST_CASE("a flipper on a crossing alternates whatever the lane") {
    const Level level = MakeLevel(CrossLevel("B*4", "B", 200, "B", 3, "B", 2, "\"flipper\"", 16));
    Board board(level);
    CHECK(!RunBoard(board).outcome.solved);  // straight on: 4 + 1, the right cup spills
    board.Stop();
    REQUIRE(board.Place(PieceOf(board, PieceType::Flipper), NodeOf(level, "x")));
    const Run run = RunBoard(board);
    CHECK(run.outcome.solved);  // left, right, left, right, then the right lane's drop goes left
  }

  TEST_CASE("an overflow on a crossing counts whatever the lane") {
    const Level level =
        MakeLevel(CrossLevel("B*5", "B", 200, "B", 4, "B", 2, "{\"type\": \"overflow\", \"n\": 2}", 16));
    Board board(level);
    const int overflow = PieceOf(board, PieceType::Overflow);
    REQUIRE(board.Place(overflow, NodeOf(level, "x")));
    board.SetSetting(overflow, 1);  // its way = side 1, the left cup
    const Run run = RunBoard(board);
    CHECK(run.outcome.solved);
    CHECK(board.Cup(NodeOf(level, "cl")).count == 2);
    CHECK(board.Cup(NodeOf(level, "cr")).count == 4);
  }

  TEST_CASE("routers and tanks leave a crossing shared; turnstiles, bridges and mixers don't") {
    for (const char* piece : {"separator", "flipper", "tank4", "tank8"}) {
      const Level level = MakeLevel(CrossLevel("B*4", "A*4", 1, "B", 4, "A", 4, std::string("\"") + piece + "\""));
      Board board(level);
      REQUIRE(board.Place(0, NodeOf(level, "x")));
      const Run run = RunBoard(board);
      INFO(piece);
      CHECK(run.outcome.failure == Failure::Mud);
      CHECK(run.outcome.fail_node == NodeOf(level, "x"));
    }
    for (const char* piece : {"turnstile", "bridge"}) {
      const Level level = MakeLevel(CrossLevel("B*4", "A*4", 1, "B", 4, "A", 4, std::string("\"") + piece + "\""));
      Board board(level);
      REQUIRE(board.Place(0, NodeOf(level, "x")));
      INFO(piece);
      CHECK(RunBoard(board).outcome.solved);
    }
  }

  TEST_CASE("a tank on a crossing: slower, and more time to collide") {
    // Amber reaches the crossing 3 ticks after blue: a near miss each time, until a tank keeps blue inside.
    const Level level = MakeLevel(CrossLevel("B*4", "A*4", 4, "B", 4, "A", 4, "\"tank4\""));
    Board board(level);
    const Run empty = RunBoard(board);
    CHECK(empty.outcome.solved);
    CHECK(empty.Count(EventType::NearMiss) == 4);
    board.Stop();
    REQUIRE(board.Place(0, NodeOf(level, "x")));
    const Run run = RunBoard(board);
    CHECK(run.outcome.failure == Failure::Mud);
    const int x = NodeOf(level, "x");
    CHECK(run.FirstTick(EventType::Collide, x) ==
          empty.FirstTick(EventType::Enter, x, Color::Amber));  // amber comes in
  }

  TEST_CASE("a tank on a merge holds drops longer: same colours still share it") {
    const Level level = MakeLevel(Merge("B*3", "B*3", 6, "B", "\"tank8\""));
    Board board(level);
    const Run empty = RunBoard(board);
    board.Stop();
    REQUIRE(board.Place(0, NodeOf(level, "m")));
    const Run run = RunBoard(board);
    CHECK(run.outcome.solved);
    const int m = NodeOf(level, "m");
    CHECK(run.FirstTick(EventType::Leave, m) == empty.FirstTick(EventType::Leave, m) + 8);
  }

  TEST_CASE("a bridge on a merge is idle: the pipes join anyway") {
    const Level level = MakeLevel(Merge("B*3", "A*3", 6, "B", "\"bridge\""));
    Board board(level);
    REQUIRE(board.Place(0, NodeOf(level, "m")));
    CHECK(board.RoleOf(0) == PieceRole::Idle);
    CHECK(RunBoard(board).outcome.failure == Failure::Mud);
  }

  TEST_CASE("an idle piece changes nothing") {
    const Level level = MakeLevel(kTwoCups);
    Board empty(level), idle(level);
    REQUIRE(idle.Place(PieceOf(idle, PieceType::Separator), NodeOf(level, "t")));  // a pipe socket
    CHECK(idle.RoleOf(PieceOf(idle, PieceType::Separator)) == PieceRole::Idle);
    const Run a = RunBoard(empty), b = RunBoard(idle);
    REQUIRE(a.timed.size() == b.timed.size());
    bool same = true;
    for (size_t i = 0; i < a.timed.size(); ++i) {
      same &= a.timed[i].first == b.timed[i].first && a.timed[i].second.type == b.timed[i].second.type &&
              a.timed[i].second.node == b.timed[i].second.node && a.timed[i].second.bead == b.timed[i].second.bead;
    }
    CHECK(same);
    CHECK(b.outcome.pieces == 1);  // it still counts for Lean
  }

  TEST_CASE("forks: the flap routes unless a router sits there; a tank holds, then the flap sends") {
    const Level level = MakeLevel(FlapFork("\"turnstile\", \"tank4\", \"flipper\", \"bridge\""));
    const int f = NodeOf(level, "f");
    Board board(level);
    const Run empty = RunBoard(board);
    CHECK(empty.outcome.solved);  // flap 0: everything to the cup
    for (PieceType type : {PieceType::Turnstile, PieceType::Bridge, PieceType::TankShort}) {
      board.Stop();
      board.ClearPieces();
      REQUIRE(board.Place(PieceOf(board, type), f));
      INFO(PieceKey(type));
      CHECK(board.FlapInCharge(f));
      CHECK(board.FlapTappable(f));
      board.SetFlap(f, 1);
      CHECK(!RunBoard(board).outcome.solved);  // flap 1: everything down the drain
      board.Stop();
      board.SetFlap(f, 0);
      const Run run = RunBoard(board);
      CHECK(run.outcome.solved);
      const int held = run.FirstTick(EventType::Leave, f) - run.FirstTick(EventType::Enter, f);
      CHECK(held == (type == PieceType::TankShort ? 5 : 1));
    }
    board.Stop();
    board.ClearPieces();
    REQUIRE(board.Place(PieceOf(board, PieceType::Flipper), f));
    CHECK(!board.FlapInCharge(f));
    CHECK(!board.FlapTappable(f));
    const Run run = RunBoard(board);
    CHECK(run.outcome.failure == Failure::Dry);  // left, right, left: two in the cup
  }

  TEST_CASE("a mixer on a crossing mixes, and each drop goes on along its own lane") {
    const Level level = MakeLevel(CrossLevel("B*4", "A*4", 1, "G", 4, "G", 4, "\"mixer\""));
    Board board(level);
    REQUIRE(board.Place(0, NodeOf(level, "x")));
    const Run run = RunBoard(board);
    CHECK(run.outcome.solved);
    CHECK(run.Count(EventType::Mix) == 4);
    CHECK(board.Cup(NodeOf(level, "cr")).count == 4);
    CHECK(board.Cup(NodeOf(level, "cl")).count == 4);
  }

  TEST_CASE("traps: a mixer or twin gate with one way in waits forever") {
    struct Case {
      std::string level;
      const char* node;
    };
    const Case cases[] = {{FlapFork("\"mixer\""), "f"},
                          {FlapFork("\"twin\""), "f"},
                          {PipeSocket("\"mixer\""), "p"},
                          {PipeSocket("\"twin\""), "p"}};
    for (const Case& c : cases) {
      const Level level = MakeLevel(c.level);
      Board board(level);
      REQUIRE(board.Place(0, NodeOf(level, c.node)));
      INFO(level.id << " " << PieceKey(level.tray[0].piece.type));
      CHECK(board.RoleOf(0) == PieceRole::Trap);
      const Run run = RunBoard(board);
      CHECK(run.outcome.jammed);
      CHECK(run.outcome.failure == Failure::Jam);
      CHECK(run.outcome.fail_node == NodeOf(level, c.node));
      CHECK(board.Jam().lonely == NodeOf(level, c.node));
    }
  }

  TEST_CASE("a twin gate on a crossing lets both lanes in together") {
    // Amber reaches the crossing 3 ticks after blue: without the gate they pass by a hair; with it blue
    // waits and both go in together, which makes mud.
    {
      const Level level = MakeLevel(CrossLevel("B*4", "A*4", 4, "B", 4, "A", 4, "\"twin\""));
      Board board(level);
      CHECK(RunBoard(board).outcome.solved);
      board.Stop();
      REQUIRE(board.Place(0, NodeOf(level, "x")));
      const Run run = RunBoard(board);
      CHECK(run.outcome.failure == Failure::Mud);
    }
    {  // one colour in both lanes: fine, and every pair enters on the same tick
      const Level level = MakeLevel(CrossLevel("B*4", "B*4", 4, "B", 4, "B", 4, "\"twin\""));
      Board board(level);
      REQUIRE(board.Place(0, NodeOf(level, "x")));
      const Run run = RunBoard(board);
      CHECK(run.outcome.solved);
      std::vector<int> left, right;
      for (const auto& [tick, e] : run.timed) {
        if (e.type != EventType::Enter || e.node != NodeOf(level, "x")) continue;
        (e.pipe == level.nodes[static_cast<size_t>(NodeOf(level, "x"))].ins[0] ? left : right).push_back(tick);
      }
      CHECK(left.size() == 4);
      CHECK(left == right);
    }
  }

  TEST_CASE("a twin gate on a merge: both enter together, so they must be one colour") {
    {
      const Level level = MakeLevel(Merge("B*3", "B*3", 6, "B", "\"twin\""));
      Board board(level);
      REQUIRE(board.Place(0, NodeOf(level, "m")));
      CHECK(RunBoard(board).outcome.solved);
    }
    {
      const Level level = MakeLevel(Merge("B*3", "A*3", 6, "B", "\"twin\""));
      Board board(level);
      REQUIRE(board.Place(0, NodeOf(level, "m")));
      CHECK(RunBoard(board).outcome.failure == Failure::Mud);
    }
  }

  TEST_CASE("a twin pair: the twin gate takes both halves, another piece only its own") {
    std::string json = kTwins;
    json.replace(json.find("\"tray\": [\"twin\"]"), 16, "\"tray\": [\"twin\", \"tank4\"]");
    const Level level = MakeLevel(json);
    const int p = NodeOf(level, "p"), q = NodeOf(level, "q");
    Board board(level);
    const int gate = PieceOf(board, PieceType::TwinGate), tank = PieceOf(board, PieceType::TankShort);
    REQUIRE(board.Place(gate, q));
    CHECK(board.PieceAt(p) == gate);
    REQUIRE(board.Place(tank, p));  // takes p: the gate goes back to the tray, q is free
    CHECK(board.PieceNode(gate) == -1);
    CHECK(board.PieceAt(q) == -1);
    CHECK(board.RoleOf(tank) == PieceRole::Acts);
    const Run run = RunBoard(board);
    CHECK(run.outcome.solved);
    CHECK(run.FirstTick(EventType::Leave, p) - run.FirstTick(EventType::Enter, p) == 5);  // held 1 + 4
  }

  TEST_CASE("the solver leaves idle and trapped pieces out") {
    // On a fork, a turnstile and a bridge are idle and a mixer is a trap: only the flap is searched.
    const Level level = MakeLevel(FlapFork("\"turnstile\", \"bridge\", \"mixer\""));
    int visits = 0;
    ForEachPlacement(level, [&](const Placement& placement) {
      ++visits;
      for (int at : placement.at) CHECK(at == -1);
      return true;
    });
    CHECK(visits == 2);  // flap 0, flap 1
    // A tank acts on the fork, and leaves its flap in charge: tray or fork, times two flaps.
    const Level tank = MakeLevel(FlapFork("\"tank4\""));
    visits = 0;
    ForEachPlacement(tank, [&](const Placement&) { return ++visits > 0; });
    CHECK(visits == 4);
  }
}
