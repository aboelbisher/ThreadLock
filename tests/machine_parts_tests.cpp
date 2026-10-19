// Tests for the machine parts (DESIGN.md §4): the turning rack, mill wheels geared to gates, and
// their level files.
#include <algorithm>
#include <string>
#include <vector>

#include "doctest.h"
#include "test_levels.h"

using namespace threadlock;

TEST_SUITE("machine parts") {
  TEST_CASE("a turning rack: a drop lands in the bottle under the pipe when it arrives") {
    const Level level = MakeLevel(kRack);
    const int r = NodeOf(level, "r");
    Board board(level);
    const Run run = RunBoard(board);
    CHECK(run.outcome.solved);
    std::vector<int> slots;
    for (const auto& [tick, e] : run.timed) {
      if (e.type != EventType::Land) continue;
      CHECK(e.node == r);
      CHECK(e.slot == (tick / 8) % 3);  // (first + t / period) % bottles
      slots.push_back(e.slot);
    }
    CHECK(slots == std::vector<int>{0, 0, 1, 1, 2, 2});
    CHECK(run.Ticks(EventType::Land, r) == std::vector<int>{25, 29, 33, 37, 41, 45});
    CHECK(run.Count(EventType::CupFull, r) == 3);
    for (int s = 0; s < 3; ++s) CHECK(board.Bottle(r, s).count == 2);
    // It turns every 8 ticks, whatever lands.
    const std::vector<int> turns = run.Ticks(EventType::Turn, r);
    REQUIRE(!turns.empty());
    CHECK(turns.front() == 8);
    for (const auto& [tick, e] : run.timed) {
      if (e.type == EventType::Turn) CHECK(e.slot == (tick / 8) % 3);
    }
  }

  TEST_CASE("a turning rack: tanks pick the slice; spill and dry count per bottle") {
    const Level level = MakeLevel(kRack);
    const int r = NodeOf(level, "r"), t = NodeOf(level, "t");
    Board board(level);
    // A short tank holds each drop 5 ticks in the socket: they land at 29, 34, 39, 44, 49, 54, and the
    // first bottle gets three.
    REQUIRE(board.Place(PieceOf(board, PieceType::TankShort), t));
    const Run short_run = RunBoard(board);
    CHECK(short_run.outcome.failure == Failure::Spill);
    CHECK(short_run.outcome.fail_node == r);
    CHECK(short_run.outcome.fail_slot == 0);
    CHECK(short_run.outcome.fail_tick == 54);
    CHECK(board.Bottle(r, 0).spilled);
    CHECK(board.Bottle(r, 2).count == 1);
    // A tall one spaces them 9 ticks apart: one bottle later each time, two each again.
    board.Stop();
    REQUIRE(board.Place(PieceOf(board, PieceType::TankTall), t));
    const Run tall_run = RunBoard(board);
    CHECK(tall_run.outcome.solved);
    CHECK(tall_run.outcome.ticks > short_run.outcome.ticks);
  }

  TEST_CASE("a rack bottle ending short is dry, a wrong colour fails it") {
    std::string json = kRack;
    json.replace(json.find("\"beads\": \"B*6\""), 14, "\"beads\": \"B*5\"");
    const Level dry = MakeLevel(json);
    Board board(dry);
    const Run run = RunBoard(board);
    CHECK(run.outcome.failure == Failure::Dry);
    CHECK(run.outcome.fail_slot == 2);
    json = kRack;
    json.replace(json.find("\"beads\": \"B*6\""), 14, "\"beads\": \"B*4 A B\"");
    const Level wrong = MakeLevel(json);
    Board other(wrong);
    const Run wrong_run = RunBoard(other);
    CHECK(wrong_run.outcome.failure == Failure::WrongCup);
    CHECK(wrong_run.outcome.fail_slot == 2);
  }

  TEST_CASE("a mill flips its gate at the end of the tick its drop leaves") {
    const Level level = MakeLevel(kMillGate);
    const int m = NodeOf(level, "m"), g = NodeOf(level, "g");
    Board board(level);
    CHECK(!board.GateOpen(g));
    const Run run = RunBoard(board);
    CHECK(run.outcome.solved);
    const std::vector<int> leaves = run.Ticks(EventType::Leave, m);
    const std::vector<int> flips = run.Ticks(EventType::Flip, g);
    REQUIRE(leaves.size() == 2);
    CHECK(flips == leaves);  // one flip per drop through the mill
    std::vector<int> states;
    for (const auto& [tick, e] : run.timed) {
      if (e.type == EventType::Flip) states.push_back(e.value);
    }
    CHECK(states == std::vector<int>{1, 0});  // opens, then shuts
    // Blue waited at the shut gate and went in on the tick after it opened, not the same tick.
    CHECK(run.FirstTick(EventType::Wait, g, Color::Blue) >= 0);
    CHECK(run.FirstTick(EventType::Enter, g, Color::Blue) == flips[0] + 1);
    CHECK(board.MillTurns(m) == 2);
    CHECK(!board.GateOpen(g));
  }

  TEST_CASE("a gate nothing will open: THREADLOCKED, the gate waits forever") {
    // The one amber drop shuts the open gate long before blue gets there.
    const Level level = MakeLevel(R"({"id": "t-shut", "nodes": [
      {"id": "sa", "kind": "spout", "at": [90, 40], "beads": "A", "beat": 12},
      {"id": "m", "kind": "mill", "at": [90, 250], "gears": ["g"]},
      {"id": "ca", "kind": "cup", "at": [90, 480], "want": "A", "count": 1},
      {"id": "sb", "kind": "spout", "at": [270, 40], "beads": "B", "beat": 12, "start": 40},
      {"id": "g", "kind": "gate", "at": [270, 150]},
      {"id": "cb", "kind": "cup", "at": [270, 480], "want": "B", "count": 1}],
      "pipes": [["sa", "m"], ["m", "ca"], ["sb", "g"], ["g", "cb"]], "tray": []})");
    Board board(level);
    const Run run = RunBoard(board);
    CHECK(run.outcome.jammed);
    CHECK(board.Jam().lonely == NodeOf(level, "g"));
    CHECK(run.outcome.fail_node == NodeOf(level, "g"));
  }

  TEST_CASE("Two Mills: both gates shut is a circle of waiting; one open and they take turns") {
    {
      const Level level = MakeLevel(TwoMills(false));
      Board board(level);
      const Run run = RunBoard(board);
      CHECK(run.outcome.jammed);
      const JamReport jam = board.Jam();
      CHECK(jam.lonely == -1);
      REQUIRE(jam.cycle.size() == 2);
      CHECK(std::count(jam.cycle.begin(), jam.cycle.end(), NodeOf(level, "ga")) == 1);
      CHECK(std::count(jam.cycle.begin(), jam.cycle.end(), NodeOf(level, "gb")) == 1);
    }
    {
      const Level level = MakeLevel(TwoMills(true));
      Board board(level);
      const Run run = RunBoard(board);
      CHECK(run.outcome.solved);
      CHECK(run.Count(EventType::Flip) == 4);
    }
  }

  TEST_CASE("racks, mills and gates in level files") {
    for (const std::string& json : {kRack, kMillGate, TwoMills(true)}) {
      const Level level = MakeLevel(json);
      Level copy;
      std::vector<std::string> errors;
      INFO(level.id);
      REQUIRE(ParseLevel(WriteLevel(level), copy, errors));
      REQUIRE(copy.nodes.size() == level.nodes.size());
      for (size_t n = 0; n < level.nodes.size(); ++n) {
        const Node& a = level.nodes[n];
        const Node& b = copy.nodes[n];
        CHECK((a.kind == b.kind && a.slots == b.slots && a.period == b.period && a.first == b.first));
        CHECK((a.gears == b.gears && a.open == b.open));
      }
    }
    const Level rack = MakeLevel(kRack);
    const Node& r = rack.nodes[static_cast<size_t>(NodeOf(rack, "r"))];
    CHECK(r.slots.size() == 3);
    CHECK(r.slots[0].order == "Mrs Hale");
    CHECK(rack.CupCount() == 3);
  }

  TEST_CASE("bad machine parts are reported") {
    const std::string head = R"({"id": "x", "nodes": [{"id": "s", "kind": "spout", "at": [180, 40], "beads": "B"},)";
    const std::string bad[] = {
        // a mill geared to a cup
        head + R"({"id": "m", "kind": "mill", "at": [180, 200], "gears": ["c"]},
          {"id": "c", "kind": "cup", "at": [180, 400], "want": "B", "count": 1}], "pipes": [["s", "m"], ["m", "c"]]})",
        // a socket on a cup
        head + R"({"id": "c", "kind": "cup", "at": [180, 400], "want": "B", "count": 1, "socket": true}],
          "pipes": [["s", "c"]]})",
        // a rack without bottles
        head + R"({"id": "r", "kind": "rack", "at": [180, 400], "slots": []}], "pipes": [["s", "r"]]})",
        // a rack starting at a bottle it doesn't have
        head + R"({"id": "r", "kind": "rack", "at": [180, 400], "first": 2, "slots": [{"want": "B", "count": 1}]}],
          "pipes": [["s", "r"]]})",
        // a gate with two ways out
        head + R"({"id": "g", "kind": "gate", "at": [180, 200]}, {"id": "c", "kind": "cup", "at": [90, 400],
          "want": "B", "count": 1}, {"id": "d", "kind": "drain", "at": [270, 400]}],
          "pipes": [["s", "g"], ["g", "c"], ["g", "d"]]})",
        // gears on a pipe socket
        head + R"({"id": "p", "kind": "pass", "at": [180, 200], "gears": ["g"]}, {"id": "g", "kind": "gate",
          "at": [180, 300]}, {"id": "c", "kind": "cup", "at": [180, 450], "want": "B", "count": 1}],
          "pipes": [["s", "p"], ["p", "g"], ["g", "c"]]})",
    };
    for (const std::string& json : bad) {
      Level level;
      std::vector<std::string> errors;
      INFO(json);
      CHECK(!ParseLevel(json, level, errors));
      CHECK(!errors.empty());
    }
  }
}
