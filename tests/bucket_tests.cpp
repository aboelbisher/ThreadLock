// Tests for World 2's round-4 rules (DESIGN.md v4 §3-§4): prescriptions (so many of each colour) and the tipping
// bucket (fills with n, tips, pours one train; on a fork the doses alternate; one that never fills: Never tipped).
#include <string>
#include <vector>

#include "../src/solver.h"
#include "doctest.h"
#include "test_levels.h"

using namespace threadlock;

namespace {

// The round-4 mock "The Half-Full Bucket": seven drops, B A B A B A B; Mrs Hale wants 2 blue + 1 amber, Baker 1 + 1,
// the rest down the sink. An overflow of 3 then a bucket of 2 measures it out; a bucket of 3 strands the seventh.
const std::string kHalfFull = R"({
  "id": "t-half-full", "name": "The Half-Full Bucket",
  "nodes": [
    {"id": "j", "kind": "spout", "at": [60, 56], "beads": "B A B A B A B", "beat": 3},
    {"id": "f1", "kind": "fork", "at": [180, 120], "socket": true},
    {"id": "p1", "kind": "pass", "at": [262, 196]},
    {"id": "f2", "kind": "fork", "at": [262, 272], "socket": true},
    {"id": "x", "kind": "cup", "at": [70, 404], "want": {"B": 2, "A": 1}, "order": "Mrs Hale"},
    {"id": "y", "kind": "cup", "at": [190, 404], "want": {"A": 1, "B": 1}, "order": "Baker"},
    {"id": "s", "kind": "drain", "at": [314, 410]}
  ],
  "pipes": [["j", "f1", [[180, 56]]], ["f1", "x", [[70, 120]]], ["f1", "p1", [[262, 120]]], ["p1", "f2"],
            ["f2", "y", [[190, 272]]], ["f2", "s", [[314, 272]]]],
  "tray": [{"type": "bucket", "n": 3}, {"type": "bucket", "n": 2}, {"type": "overflow", "n": 3}, "flipper"]
})";

}  // namespace

TEST_SUITE("buckets") {
  TEST_CASE("a prescription reads, counts and writes back") {
    const Level level = MakeLevel(kHalfFull);
    const Node& x = level.nodes[static_cast<size_t>(NodeOf(level, "x"))];
    CHECK(x.count == 3);
    CHECK(x.want == Color::Blue);
    CHECK(x.wants[static_cast<size_t>(Color::Blue)] == 2);
    CHECK(x.wants[static_cast<size_t>(Color::Amber)] == 1);
    const std::string written = WriteLevel(level);
    CHECK(written.find("\"want\": {\"B\": 2, \"A\": 1}") != std::string::npos);
    CHECK(written.find("{\"type\": \"bucket\", \"n\": 3}") != std::string::npos);
    const Level again = MakeLevel(written);
    CHECK(again.nodes[static_cast<size_t>(NodeOf(again, "x"))].wants == x.wants);
    // A one-colour cup keeps its old form.
    CHECK(level.nodes[static_cast<size_t>(NodeOf(level, "y"))].wants[static_cast<size_t>(Color::Amber)] == 1);
    Level bad;
    std::vector<std::string> errors;
    std::string json = kHalfFull;
    json.replace(json.find("{\"type\": \"bucket\", \"n\": 3}"), 26, "{\"type\": \"bucket\", \"n\": 5}");
    CHECK(!ParseLevel(json, bad, errors));
  }

  TEST_CASE("the empty board: everything goes left, Mrs Hale's second amber spills") {
    const Level level = MakeLevel(kHalfFull);
    Board board(level);
    const Run run = RunBoard(board);
    CHECK(run.outcome.failure == Failure::Spill);
    CHECK(run.outcome.fail_node == NodeOf(level, "x"));
  }

  TEST_CASE("an overflow of 3 and a bucket of 2 measure out both prescriptions") {
    const Level level = MakeLevel(kHalfFull);
    Board board(level);
    REQUIRE(board.Place(PieceOf(board, PieceType::Overflow), NodeOf(level, "f1")));
    const int bucket = PieceOf(board, PieceType::Bucket, 1);
    REQUIRE(board.Place(bucket, NodeOf(level, "f2")));
    CHECK(board.RoleOf(bucket) == PieceRole::Acts);
    const Run run = RunBoard(board);
    CHECK(run.outcome.solved);
    CHECK(run.Count(EventType::Tip, NodeOf(level, "f2")) == 2);
    CHECK(run.Count(EventType::CupFull) == 2);
    // A dose leaves as one train: the two drops of a tip land on consecutive ticks.
    const std::vector<int> lands = run.Ticks(EventType::Land, NodeOf(level, "y"));
    REQUIRE(lands.size() == 2);
    CHECK(lands[1] == lands[0] + 1);
  }

  TEST_CASE("a bucket of 3 on the first fork: two doses, then the seventh drop sits in it forever") {
    const Level level = MakeLevel(kHalfFull);
    Board board(level);
    REQUIRE(board.Place(PieceOf(board, PieceType::Bucket, 0), NodeOf(level, "f1")));
    REQUIRE(board.Place(PieceOf(board, PieceType::Bucket, 1), NodeOf(level, "f2")));
    const Run run = RunBoard(board);
    CHECK(run.outcome.failure == Failure::HalfFull);
    CHECK(run.outcome.fail_node == NodeOf(level, "f1"));
    CHECK(board.InsideCount(NodeOf(level, "f1")) == 1);
    CHECK(board.Jam().half_full == NodeOf(level, "f1"));
    CHECK(run.Count(EventType::Tip, NodeOf(level, "f1")) == 2);
  }

  TEST_CASE("a bucket on a fork alternates whole doses, starting on its side") {
    const std::string json = R"({"id": "t-doses",
      "nodes": [{"id": "s", "kind": "spout", "at": [180, 40], "beads": "B*6", "beat": 3},
                {"id": "f", "kind": "fork", "at": [180, 140], "socket": true},
                {"id": "l", "kind": "cup", "at": [70, 400], "want": "B", "count": 3},
                {"id": "r", "kind": "cup", "at": [290, 400], "want": "B", "count": 3}],
      "pipes": [["s", "f"], ["f", "l", [[70, 140]]], ["f", "r", [[290, 140]]]],
      "tray": [{"type": "bucket", "n": 3}]})";
    const Level level = MakeLevel(json);
    for (int side = 0; side < 2; ++side) {
      Board board(level);
      REQUIRE(board.Place(0, NodeOf(level, "f")));
      board.SetSetting(0, side);
      const Run run = RunBoard(board);
      CHECK(run.outcome.solved);
      const int first = run.FirstTick(EventType::Land, NodeOf(level, side == 0 ? "l" : "r"));
      const int second = run.FirstTick(EventType::Land, NodeOf(level, side == 0 ? "r" : "l"));
      CHECK(first < second);
    }
  }

  TEST_CASE("a prescription: a colour it doesn't want is wrong, one too many of a colour spills") {
    const std::string json = R"({"id": "t-rx",
      "nodes": [{"id": "s", "kind": "spout", "at": [180, 40], "beads": "B A B", "beat": 4},
                {"id": "c", "kind": "cup", "at": [180, 400], "want": {"B": 1, "A": 2}}],
      "pipes": [["s", "c"]], "tray": []})";
    const Level spill = MakeLevel(json);
    Board board(spill);
    CHECK(RunBoard(board).outcome.failure == Failure::Spill);  // the second blue
    std::string rose = json;
    rose.replace(rose.find("B A B"), 5, "B A R");
    const Level rose_level = MakeLevel(rose);
    Board wrong(rose_level);
    CHECK(RunBoard(wrong).outcome.failure == Failure::WrongCup);
    std::string blend = json;
    blend.replace(blend.find("B A B"), 5, "B+A A");
    const Level blend_level = MakeLevel(blend);
    Board both(blend_level);  // a blend counts one of each
    CHECK(RunBoard(both).outcome.solved);
  }

  TEST_CASE("the solver: an overflow of 3 and the bucket of 2 are the Lean machine") {
    const SolveReport report = Solve(MakeLevel(kHalfFull));
    CHECK(report.pars.pieces == 2);
    CHECK(report.solutions == 2);  // the bucket's first side doesn't matter
  }
}
