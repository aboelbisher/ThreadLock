// Tests for World 3's round-4 rules (DESIGN.md v4 §4): two-way canals one barge wide. Barges sail to their harbour,
// standoffs freeze a narrow, basins hold berths, a signal takes one direction at a time, a passing place lets two pass,
// a bridge splits a crossing into two lanes. Nothing mixes.
#include <string>
#include <vector>

#include "../src/solver.h"
#include "doctest.h"
#include "test_levels.h"

using namespace threadlock;

namespace {

// The pitch's "The Narrows": blue barges from the north wharf sail to the south harbour, amber from the south to the
// north, through one narrow canal with a socket in its middle. Each end is a basin with its wharf and its harbour (four
// berths: with two, the ambers waiting at the south basin fill it, and the blue convoy can't get in).
std::string Narrows(const std::string& tray, int berths = 4) {
  return R"({
  "id": "t-narrows", "name": "The Narrows", "traffic": "two-way",
  "nodes": [
    {"id": "nw", "kind": "spout", "at": [100, 30], "beads": "B*3", "beat": 6},
    {"id": "nh", "kind": "cup", "at": [260, 30], "want": "A", "count": 3},
    {"id": "kn", "kind": "basin", "at": [180, 110], "berths": )" +
         std::to_string(berths) + R"(},
    {"id": "p", "kind": "pass", "at": [180, 260]},
    {"id": "ks", "kind": "basin", "at": [180, 410], "berths": )" +
         std::to_string(berths) + R"(},
    {"id": "sw", "kind": "spout", "at": [260, 500], "beads": "A*3", "beat": 6, "start": 4},
    {"id": "sh", "kind": "cup", "at": [100, 500], "want": "B", "count": 3}
  ],
  "pipes": [["nw", "kn", [[100, 110]]], ["kn", "nh", [[260, 110]]], ["kn", "p"], ["p", "ks"], ["sw", "ks", [[260, 410]]],
            ["ks", "sh", [[100, 410]]]],
  "tray": [)" + tray +
         R"(]
})";
}

const char* kTray = R"("signal", "passing")";

// Old Bridge: an east-west canal and a north-south one cross at x, barges both ways on both. Without a bridge the
// crossing is one place for both canals; with one, each canal runs straight on over it and its signal guards it end to
// end.
const std::string kOldBridge = R"({
  "id": "t-bridge", "name": "Old Bridge", "traffic": "two-way",
  "nodes": [
    {"id": "kw", "kind": "basin", "at": [60, 300], "berths": 3},
    {"id": "ke", "kind": "basin", "at": [300, 300], "berths": 4},
    {"id": "kn", "kind": "basin", "at": [180, 130], "berths": 3},
    {"id": "ks", "kind": "basin", "at": [180, 470], "berths": 1},
    {"id": "x", "kind": "cross", "at": [180, 300], "socket": true},
    {"id": "pw", "kind": "pass", "at": [120, 300]},
    {"id": "pe", "kind": "pass", "at": [240, 300]},
    {"id": "pn", "kind": "pass", "at": [180, 215]},
    {"id": "ps", "kind": "pass", "at": [180, 385]},
    {"id": "ww", "kind": "spout", "at": [60, 225], "beads": "B*3", "beat": 2, "start": 6},
    {"id": "he", "kind": "cup", "at": [300, 210], "want": "B", "count": 3},
    {"id": "we", "kind": "spout", "at": [300, 390], "beads": "A*3", "beat": 3, "start": 4},
    {"id": "hw", "kind": "cup", "at": [60, 390], "want": "A", "count": 3},
    {"id": "wn", "kind": "spout", "at": [180, 45], "beads": "G", "beat": 5, "start": 4},
    {"id": "hs", "kind": "cup", "at": [270, 470], "want": "G", "count": 1},
    {"id": "ws", "kind": "spout", "at": [180, 560], "beads": "R*3", "beat": 5, "start": 3},
    {"id": "hn", "kind": "cup", "at": [90, 130], "want": "R", "count": 3}
  ],
  "pipes": [["kw", "pw"], ["pw", "x"], ["x", "pe"], ["pe", "ke"], ["kn", "pn"], ["pn", "x"], ["x", "ps"], ["ps", "ks"],
            ["ww", "kw"], ["ke", "he"], ["we", "ke"], ["kw", "hw"], ["wn", "kn"], ["ks", "hs"], ["ws", "ks"], ["kn", "hn"]],
  "tray": ["bridge", "signal", "signal", "passing"]
})";

}  // namespace

TEST_SUITE("canal traffic") {
  TEST_CASE("a two-way level reads and writes back: traffic, berths, a wharf's harbour") {
    std::string json = Narrows(kTray, 3);
    json.replace(json.find(R"("beat": 6},)"), 11, R"("beat": 6, "to": "sh"},)");
    const Level level = MakeLevel(json);
    CHECK(level.two_way);
    CHECK(level.nodes[static_cast<size_t>(NodeOf(level, "kn"))].berths == 3);
    CHECK(level.nodes[static_cast<size_t>(NodeOf(level, "nw"))].to == NodeOf(level, "sh"));
    CHECK(level.nodes[static_cast<size_t>(NodeOf(level, "p"))].dwell == 1);
    const std::string written = WriteLevel(level);
    CHECK(written.find("\"traffic\": \"two-way\"") != std::string::npos);
    CHECK(written.find("\"berths\": 3") != std::string::npos);
    CHECK(written.find("\"to\": \"sh\"") != std::string::npos);
    const Level again = MakeLevel(written);
    CHECK(again.two_way);
    // A basin needs two-way canals; two-way canals have no forks.
    Level bad;
    std::vector<std::string> errors;
    std::string one_way = Narrows(kTray);
    one_way.replace(one_way.find(R"("traffic": "two-way",)"), 21, "");
    CHECK(!ParseLevel(one_way, bad, errors));
  }

  TEST_CASE("the empty board: blue and amber meet nose to nose in the narrow") {
    const Level level = MakeLevel(Narrows(kTray));
    Board board(level);
    const Run run = RunBoard(board);
    CHECK(run.outcome.jammed);
    CHECK(run.outcome.failure == Failure::Jam);
    const JamReport jam = board.Jam();
    CHECK(jam.standoff_a >= 0);
    CHECK(jam.standoff_b >= 0);
    CHECK(run.Count(EventType::Collide) == 0);  // nothing mixes
  }

  TEST_CASE("a signal: one direction at a time, convoys follow, every barge home") {
    const Level level = MakeLevel(Narrows(kTray));
    Board board(level);
    const int signal = PieceOf(board, PieceType::Signal);
    REQUIRE(board.Place(signal, NodeOf(level, "p")));
    CHECK(board.RoleOf(signal) == PieceRole::Acts);
    const Run run = RunBoard(board);
    CHECK(run.outcome.solved);
    CHECK(board.Cup(NodeOf(level, "nh")).count == 3);
    CHECK(board.Cup(NodeOf(level, "sh")).count == 3);
    // Barges route themselves: every blue lands south, every amber north.
    for (const auto& [tick, e] : run.timed) {
      if (e.type != EventType::Land) continue;
      CHECK(e.node == NodeOf(level, e.bead.a == Color::Blue ? "sh" : "nh"));
    }
  }

  TEST_CASE("the signal's arrow decides who goes first when both ends want in on one tick") {
    std::string json = Narrows(kTray);
    json.replace(json.find(R"("start": 4)"), 10, R"("start": 1)");  // both wharves pour on tick 1
    json.replace(json.find("[260, 500]"), 10, "[260, 490]");  // and their canals are the same length: a tie
    const Level level = MakeLevel(json);
    int first_land[2] = {0, 0};
    for (int setting = 0; setting < 2; ++setting) {
      Board board(level);
      const int signal = PieceOf(board, PieceType::Signal);
      REQUIRE(board.Place(signal, NodeOf(level, "p")));
      board.SetSetting(signal, setting);
      const Run run = RunBoard(board);
      CHECK(run.outcome.solved);
      first_land[setting] = run.FirstTick(EventType::Land, NodeOf(level, "sh"));
    }
    CHECK(first_land[0] != first_land[1]);
  }

  TEST_CASE("a passing place lets two barges heading opposite ways share the socket") {
    const Level level = MakeLevel(Narrows(kTray));
    Board board(level);
    REQUIRE(board.Place(PieceOf(board, PieceType::Passing), NodeOf(level, "p")));
    board.Start();
    int most = 0;
    while (board.Running()) {
      board.Tick();
      most = std::max(most, board.InsideCount(NodeOf(level, "p")));
    }
    CHECK(most <= 2);
    CHECK(Role(PieceType::Passing, NodeKind::Pass) == PieceRole::Acts);
    CHECK(Role(PieceType::Passing, NodeKind::Cross) == PieceRole::Idle);
    CHECK(Role(PieceType::Signal, NodeKind::Fork) == PieceRole::Idle);
  }

  TEST_CASE("stretches run between basins through canal sockets") {
    const Level level = MakeLevel(Narrows(kTray));
    Board board(level);
    board.Start();
    const int narrow = board.StretchOf(NodeOf(level, "p") >= 0 ? level.nodes[static_cast<size_t>(NodeOf(level, "p"))].ins[0] : 0);
    const std::array<int, 2> ends = board.StretchEnds(narrow);
    CHECK(((ends[0] == NodeOf(level, "kn") && ends[1] == NodeOf(level, "ks")) ||
           (ends[1] == NodeOf(level, "kn") && ends[0] == NodeOf(level, "ks"))));
    CHECK(board.StretchOf(level.nodes[static_cast<size_t>(NodeOf(level, "p"))].outs[0]) == narrow);
  }

  TEST_CASE("two berths at each end: the waiting ambers fill the south basin from below, the blues from above") {
    const Level level = MakeLevel(Narrows(kTray, 2));
    Board board(level);
    REQUIRE(board.Place(PieceOf(board, PieceType::Signal), NodeOf(level, "p")));
    const Run run = RunBoard(board);
    CHECK(run.outcome.jammed);
    CHECK(board.Jam().full_basin == NodeOf(level, "ks"));
    CHECK(run.outcome.fail_node == NodeOf(level, "ks"));
  }

  TEST_CASE("the solver: one piece is enough, the signal") {
    const SolveReport report = Solve(MakeLevel(Narrows(kTray)));
    CHECK(report.pars.pieces == 1);
    CHECK(report.solutions >= 1);
  }

  TEST_CASE("a bridge carries each canal straight on: one stretch from basin to basin, guarded by one signal") {
    const Level level = MakeLevel(kOldBridge);
    const int x = NodeOf(level, "x"), kw = NodeOf(level, "kw"), ke = NodeOf(level, "ke");
    const int west = level.nodes[static_cast<size_t>(NodeOf(level, "pw"))].ins[0];
    const int east = level.nodes[static_cast<size_t>(NodeOf(level, "pe"))].outs[0];
    auto ends_are = [](std::array<int, 2> ends, int a, int b) {
      return (ends[0] == a && ends[1] == b) || (ends[0] == b && ends[1] == a);
    };
    {  // no bridge: the crossing ends the stretches on both sides
      Board board(level);
      board.Start();
      CHECK(ends_are(board.StretchEnds(board.StretchOf(west)), kw, x));
      CHECK(board.StretchOf(west) != board.StretchOf(east));
    }
    Board board(level);
    REQUIRE(board.Place(PieceOf(board, PieceType::Bridge), x));
    board.Start();
    CHECK(board.StretchOf(west) == board.StretchOf(east));
    CHECK(ends_are(board.StretchEnds(board.StretchOf(west)), kw, ke));
    // The other lane is a stretch of its own, north basin to south basin.
    const int north = level.nodes[static_cast<size_t>(NodeOf(level, "pn"))].ins[0];
    CHECK(board.StretchOf(north) != board.StretchOf(west));
    CHECK(ends_are(board.StretchEnds(board.StretchOf(north)), NodeOf(level, "kn"), NodeOf(level, "ks")));
  }

  TEST_CASE("Old Bridge: the bridge and a signal on each canal, the arrows right; without the bridge, gridlock") {
    const Level level = MakeLevel(kOldBridge);
    auto play = [&](bool bridge, int west_arrow, int north_arrow) {
      Board board(level);
      if (bridge) REQUIRE(board.Place(PieceOf(board, PieceType::Bridge), NodeOf(level, "x")));
      const int a = PieceOf(board, PieceType::Signal, 0), b = PieceOf(board, PieceType::Signal, 1);
      REQUIRE(board.Place(a, NodeOf(level, "pw")));
      REQUIRE(board.Place(b, NodeOf(level, "pn")));
      board.SetSetting(a, west_arrow);
      board.SetSetting(b, north_arrow);
      return RunBoard(board).outcome;
    };
    CHECK(play(true, 0, 1).solved);
    const Outcome no_bridge = play(false, 0, 1);
    CHECK(no_bridge.jammed);
    CHECK(no_bridge.fail_node == NodeOf(level, "x"));
    CHECK(!play(true, 1, 1).solved);  // amber first: the blues fill the west basin
    CHECK(!play(true, 0, 0).solved);  // green first: the roses fill the one-berth south basin
    const SolveReport report = Solve(level);
    CHECK(report.pars.pieces == 3);
    CHECK(PlacementText(level, report.pars.lean).find("bridge@x") != std::string::npos);
  }
}
