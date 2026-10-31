// Tests for World 4's round-4 rules (DESIGN.md v4 §3-§4): recipe phials take their layers in order (a layer out
// of turn is Muddled), and the candle holds its drop until its phial holds n.
#include <string>
#include <vector>

#include "../src/solver.h"
#include "doctest.h"
#include "test_levels.h"

using namespace threadlock;

namespace {

// The pitch's board, "Green, then Rose": blue and amber meet at a globe (a mixer makes two green); rose has the
// shorter way to the phial, through a pipe socket and the globe over the phial. The recipe is green, green, rose.
std::string GreenThenRose(const std::string& tray, const std::string& recipe = "G G R") {
  return R"({
  "id": "t-green-rose", "name": "Green, then Rose",
  "nodes": [
    {"id": "sb", "kind": "spout", "at": [60, 40], "beads": "B", "beat": 12},
    {"id": "sa", "kind": "spout", "at": [300, 40], "beads": "A", "beat": 12},
    {"id": "sr", "kind": "spout", "at": [330, 200], "beads": "R", "beat": 12},
    {"id": "m", "kind": "merge", "at": [180, 150], "socket": true},
    {"id": "c", "kind": "pass", "at": [330, 330]},
    {"id": "j", "kind": "merge", "at": [180, 400]},
    {"id": "x", "kind": "cup", "at": [180, 520], "recipe": ")" +
         recipe + R"("}
  ],
  "pipes": [["sb", "m"], ["sa", "m"], ["m", "j"], ["sr", "c"], ["c", "j", [[330, 400]]], ["j", "x"]],
  "tray": [)" + tray +
         R"(]
})";
}

const char* kTray = R"("mixer", "twin", {"type": "candle", "n": 2}, {"type": "candle", "n": 3})";

}  // namespace

TEST_SUITE("recipes") {
  TEST_CASE("a recipe sets the cup's count, and reads and writes back") {
    const Level level = MakeLevel(GreenThenRose(kTray));
    const Node& x = level.nodes[static_cast<size_t>(NodeOf(level, "x"))];
    REQUIRE(x.recipe.size() == 3);
    CHECK(x.recipe[0] == Color::Green);
    CHECK(x.recipe[2] == Color::Rose);
    CHECK(x.count == 3);
    CHECK(x.want == Color::Green);
    const std::string written = WriteLevel(level);
    CHECK(written.find("\"recipe\": \"G G R\"") != std::string::npos);
    CHECK(written.find("{\"type\": \"candle\", \"n\": 2}") != std::string::npos);
    const Level again = MakeLevel(written);
    CHECK(again.nodes[static_cast<size_t>(NodeOf(again, "x"))].recipe == x.recipe);
    CHECK(again.tray.size() == level.tray.size());
  }

  TEST_CASE("a recipe replaces want and count; a candle takes n 1-4") {
    Level level;
    std::vector<std::string> errors;
    std::string json = GreenThenRose(kTray);
    json.replace(json.find("\"recipe\""), 0, "\"want\": \"G\", ");
    CHECK(!ParseLevel(json, level, errors));
    errors.clear();
    CHECK(!ParseLevel(GreenThenRose(R"({"type": "candle", "n": 5})"), level, errors));
    errors.clear();
    CHECK(!ParseLevel(GreenThenRose(kTray, "G B+A"), level, errors));  // a layer is one colour
  }

  TEST_CASE("a pipe socket feeds its cup through globes; a candle acts only there") {
    const Level level = MakeLevel(GreenThenRose(kTray));
    const Node& c = level.nodes[static_cast<size_t>(NodeOf(level, "c"))];
    CHECK(c.feeds_cup == NodeOf(level, "x"));
    CHECK(Role(PieceType::Candle, c) == PieceRole::Acts);
    CHECK(Role(PieceType::Candle, NodeKind::Pass, false, false) == PieceRole::Idle);
    CHECK(Role(PieceType::Candle, NodeKind::Fork, false, true) == PieceRole::Idle);
    CHECK(Role(PieceType::Candle, NodeKind::Merge, false, true) == PieceRole::Idle);
    CHECK(Role(PieceType::Candle, NodeKind::Cross, false, true) == PieceRole::Idle);
    // A candle doesn't change the mud rule or any other piece's role.
    CHECK(Role(PieceType::Mixer, c) == PieceRole::Trap);
  }

  TEST_CASE("the empty board: blue and amber meet in the globe with nothing to mix them") {
    const Level level = MakeLevel(GreenThenRose(kTray));
    Board board(level);
    const Run run = RunBoard(board);
    CHECK(run.outcome.failure == Failure::Mud);
    CHECK(run.outcome.fail_node == NodeOf(level, "m"));
  }

  TEST_CASE("a mixer alone: rose has the shorter way and lands first, out of turn") {
    const Level level = MakeLevel(GreenThenRose(kTray));
    Board board(level);
    REQUIRE(board.Place(PieceOf(board, PieceType::Mixer), NodeOf(level, "m")));
    const Run run = RunBoard(board);
    CHECK(!run.outcome.solved);
    CHECK(run.outcome.failure == Failure::Muddled);
    CHECK(run.outcome.fail_node == NodeOf(level, "x"));
    CHECK(run.outcome.fail_layer == 0);
    CHECK(run.outcome.fail_bead == Bead::Pure(Color::Rose));
    CHECK(run.Count(EventType::Muddle, NodeOf(level, "x")) == 1);
    CHECK(run.Count(EventType::CupFull) == 0);
  }

  TEST_CASE("a mixer and a candle of 2: rose waits for the two greens") {
    const Level level = MakeLevel(GreenThenRose(kTray));
    Board board(level);
    REQUIRE(board.Place(PieceOf(board, PieceType::Mixer), NodeOf(level, "m")));
    const int candle = PieceOf(board, PieceType::Candle, 0);
    REQUIRE(board.PieceInfo(candle).piece.n == 2);
    REQUIRE(board.Place(candle, NodeOf(level, "c")));
    CHECK(board.RoleOf(candle) == PieceRole::Acts);
    const Run run = RunBoard(board);
    CHECK(run.outcome.solved);
    CHECK(run.Count(EventType::Open, NodeOf(level, "c")) == 1);
    CHECK(run.Count(EventType::CupFull, NodeOf(level, "x")) == 1);
    // It opens on the tick the second green lands, never before.
    const std::vector<int> lands = run.Ticks(EventType::Land, NodeOf(level, "x"));
    REQUIRE(lands.size() == 3);
    CHECK(run.FirstTick(EventType::Open, NodeOf(level, "c")) >= lands[1]);
    CHECK(run.FirstTick(EventType::Land, NodeOf(level, "x"), Color::Rose) == lands[2]);
  }

  TEST_CASE("a candle of 3 waits for a third layer that only it can bring: THREADLOCKED, the candle shown") {
    const Level level = MakeLevel(GreenThenRose(kTray));
    Board board(level);
    REQUIRE(board.Place(PieceOf(board, PieceType::Mixer), NodeOf(level, "m")));
    REQUIRE(board.Place(PieceOf(board, PieceType::Candle, 1), NodeOf(level, "c")));
    const Run run = RunBoard(board);
    CHECK(run.outcome.jammed);
    CHECK(run.outcome.failure == Failure::Jam);
    CHECK(run.outcome.fail_node == NodeOf(level, "c"));
    CHECK(board.Jam().lonely == NodeOf(level, "c"));
    CHECK(board.Cup(NodeOf(level, "x")).count == 2);
    CHECK(run.Count(EventType::Open) == 0);
  }

  TEST_CASE("a blend is two layers at once: always out of turn") {
    const std::string json = R"({
      "id": "t-blend-recipe",
      "nodes": [
        {"id": "s", "kind": "spout", "at": [180, 40], "beads": "B+A", "beat": 12},
        {"id": "x", "kind": "cup", "at": [180, 300], "recipe": "A B"}
      ],
      "pipes": [["s", "x"]], "tray": []
    })";
    const Level level = MakeLevel(json);
    Board board(level);
    const Run run = RunBoard(board);
    CHECK(run.outcome.failure == Failure::Muddled);
    CHECK(run.outcome.fail_bead.IsBlend());
  }

  TEST_CASE("the kit check: a world's own pieces only; a candle needs a groove into a phial") {
    Level level = MakeLevel(GreenThenRose(kTray));
    level.world = 4;
    CHECK(KitProblems(level).empty());
    CHECK(InKit(4, PieceType::Candle));
    CHECK(!InKit(4, PieceType::TankShort));
    CHECK(!InKit(1, PieceType::Mixer));
    CHECK(InKit(0, PieceType::Mixer));  // the Lab keeps the mixed kit
    level.world = 1;                    // World 1 is enforced: a mixer and a candle are out of its kit
    CHECK(KitProblems(level).size() == 4);
    level.world = 4;
    level.nodes[static_cast<size_t>(NodeOf(level, "c"))].socket = false;  // no socket left that leads to the phial
    const std::vector<std::string> problems = KitProblems(level);
    REQUIRE(!problems.empty());
    CHECK(problems.back() == "a candle is in the tray, but no pipe socket leads to a cup");
  }

  TEST_CASE("the solver: a mixer and the candle of 2 are the one Lean machine") {
    Level level = MakeLevel(GreenThenRose(kTray));
    const SolveReport result = Solve(level);
    CHECK(result.pars.pieces == 2);
    CHECK(result.pars.solutions == 1);
  }
}
