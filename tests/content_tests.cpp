// The game's content: every shipped level (assets/levels) is valid, solvable and matches the pars
// file; generated levels (daily, Lab) are deterministic, valid and solvable.
#include <algorithm>
#include <chrono>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "../src/apothecary_look.h"
#include "../src/generator.h"
#include "../src/level_io.h"
#include "../src/solver.h"
#include "doctest.h"

#ifndef THREADLOCK_ASSETS
#define THREADLOCK_ASSETS "assets"
#endif

using namespace threadlock;

namespace {

std::string ReadText(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  std::stringstream text;
  text << file.rdbuf();
  return text.str();
}

std::vector<World> LoadAll() {
  std::vector<World> worlds;
  for (int n = 1; n <= 8; ++n) {
    const std::string text = ReadText(std::string(THREADLOCK_ASSETS) + "/levels/world" + std::to_string(n) + ".json");
    if (text.empty()) break;
    World world;
    std::vector<std::string> errors;
    const bool ok = ParseWorld(text, world, errors);
    for (const std::string& error : errors) INFO(error);
    CHECK(ok);
    worlds.push_back(std::move(world));
  }
  std::vector<std::string> errors;
  CHECK(ApplyPars(ReadText(std::string(THREADLOCK_ASSETS) + "/levels/pars.json"), worlds, errors));
  return worlds;
}

bool Has(const std::vector<std::string>& problems, const std::string& problem) {
  return std::find(problems.begin(), problems.end(), problem) != problems.end();
}

}  // namespace

TEST_SUITE("content") {
  TEST_CASE("every shipped level is solvable and its pars are current") {
    const std::vector<World> worlds = LoadAll();
    REQUIRE(!worlds.empty());
    int levels = 0;
    for (const World& world : worlds) {
      CHECK(world.levels.size() >= 15);
      for (const Level& level : world.levels) {
        ++levels;
        INFO("level " << level.id);
        const SolveReport report = Solve(level);
        CHECK(report.solutions > 0);
        CHECK(level.pars.pieces == report.pars.pieces);
        CHECK(level.pars.ticks == report.pars.ticks);
        // The stored solutions really solve it (hints and "show the solution" use them).
        CHECK(Simulate(level, level.pars.lean).solved);
        CHECK(Simulate(level, level.pars.swift).solved);
        const Outcome lean = Simulate(level, level.pars.lean);
        CHECK(lean.pieces == level.pars.pieces);
        // An empty board never solves a level that has pieces to place.
        bool movable = false;
        for (const TrayPiece& piece : level.tray) movable |= !piece.locked;
        Board empty(level);
        if (movable) CHECK(!Simulate(level, empty.GetPlacement(), false).solved);
      }
    }
    MESSAGE(levels << " levels checked");
  }

  TEST_CASE("the level writer keeps everything (the tool's format and search commands use it)") {
    for (const World& world : LoadAll()) {
      for (const Level& level : world.levels) {
        INFO("level " << level.id);
        Level copy;
        std::vector<std::string> errors;
        REQUIRE(ParseLevel(WriteLevel(level), copy, errors));
        CHECK(copy.name == level.name);
        CHECK(copy.intro == level.intro);
        CHECK(copy.type == level.type);
        CHECK(copy.look == level.look);
        REQUIRE(copy.nodes.size() == level.nodes.size());
        for (size_t n = 0; n < level.nodes.size(); ++n) {
          const Node& a = level.nodes[n];
          const Node& b = copy.nodes[n];
          CHECK((a.id == b.id && a.kind == b.kind && a.at.x == b.at.x && a.at.y == b.at.y && a.socket == b.socket));
          CHECK((a.beads == b.beads && a.beat == b.beat && a.start == b.start));
          CHECK((a.flap == b.flap && a.flap_tappable == b.flap_tappable && a.want == b.want && a.count == b.count &&
                 a.order == b.order && a.twin == b.twin));
          CHECK((a.slots == b.slots && a.period == b.period && a.first == b.first && a.gears == b.gears &&
                 a.open == b.open));
        }
        REQUIRE(copy.pipes.size() == level.pipes.size());
        for (size_t p = 0; p < level.pipes.size(); ++p) {
          CHECK((copy.pipes[p].from == level.pipes[p].from && copy.pipes[p].to == level.pipes[p].to &&
                 copy.pipes[p].cells == level.pipes[p].cells));
        }
        REQUIRE(copy.tray.size() == level.tray.size());
        for (size_t t = 0; t < level.tray.size(); ++t) {
          CHECK((copy.tray[t].piece == level.tray[t].piece && copy.tray[t].at == level.tray[t].at &&
                 copy.tray[t].setting == level.tray[t].setting && copy.tray[t].locked == level.tray[t].locked));
        }
      }
    }
  }

  TEST_CASE("a tall level's look survives the writer") {
    Level level = LoadAll().front().levels.front();
    level.look.height = 1200.0f;
    level.look.payoff = {180.0f, 870.0f};
    level.look.shelves = {150.0f, 330.0f, 1100.0f};
    Level copy;
    std::vector<std::string> errors;
    REQUIRE(ParseLevel(WriteLevel(level), copy, errors));
    CHECK(copy.look == level.look);
    // Too short a board is an error (and its shelf is now off it).
    std::string text = WriteLevel(level);
    text.replace(text.find("1200"), 4, "500");
    CHECK(!ParseLevel(text, copy, errors));
  }

  TEST_CASE("a shop wall: orders on cups and bottles, pipes straight across or down") {
    // World 2's grammar: jars on shelves, copper lines along them and straight down to the counter.
    const std::string shop = R"({"id": "t-1", "name": "Shop", "shelves": [60, 300],
      "nodes": [{"id": "s", "kind": "spout", "at": [80, 80], "beads": "B*3", "beat": 4},
                {"id": "f", "kind": "fork", "at": [180, 160], "socket": true},
                {"id": "a", "kind": "cup", "at": [90, 480], "want": "B", "count": 2, "order": "Mrs Hale"},
                {"id": "b", "kind": "cup", "at": [270, 480], "want": "B", "count": 1, "order": "Baker"}],
      "pipes": [["s", "f", [[180, 80]]], ["f", "a", [[90, 160]]], ["f", "b", [[270, 160]]]],
      "tray": [{"type": "overflow", "n": 2}]})";
    Level level;
    std::vector<std::string> errors;
    REQUIRE(ParseLevel(shop, level, errors));
    CHECK(level.nodes[2].order == "Mrs Hale");
    CHECK(level.look.shelves == std::vector<float>{60.0f, 300.0f});
    CHECK(LookProblems(level).empty());
    Level copy;
    REQUIRE(ParseLevel(WriteLevel(level), copy, errors));
    CHECK(copy.nodes[3].order == "Baker");
    CHECK(copy.look == level.look);

    // A slanted line is wrong on a shop wall, fine elsewhere (and a bottle is filled from straight above).
    std::string slanted = shop;
    slanted.replace(slanted.find("[[90, 160]]"), 11, "[[120, 200]]");
    REQUIRE(ParseLevel(slanted, level, errors));
    const std::vector<std::string> problems = LookProblems(level);
    REQUIRE(problems.size() == 2);
    CHECK(problems[0] == "pipe f -> a runs at a slant (segment 0)");
    CHECK(problems[1] == "pipe f -> a doesn't come down into the bottle");
    level.look.shelves.clear();
    CHECK(LookProblems(level).empty());

    // The orders hang on a cork board in the top right: nothing of the machine may sit behind it.
    std::string hidden = shop;
    hidden.replace(hidden.find("[180, 160]"), 10, "[300, 40]");
    REQUIRE(ParseLevel(hidden, level, errors));
    CHECK(Has(LookProblems(level), "node f is under the cork board"));
  }

  TEST_CASE("a shop wall: jars, slates, the bell and the bottles where they belong") {
    const std::string shop = R"({"id": "t-1", "name": "Shop", "shelves": [60, 300],
      "nodes": [{"id": "s", "kind": "spout", "at": [80, 80], "beads": "B*3", "beat": 4},
                {"id": "f", "kind": "fork", "at": [180, 160], "socket": true},
                {"id": "a", "kind": "cup", "at": [90, 480], "want": "B", "count": 2, "order": "Mrs Hale"},
                {"id": "b", "kind": "cup", "at": [270, 480], "want": "B", "count": 1, "order": "Baker"}],
      "pipes": [["s", "f", [[180, 80]]], ["f", "a", [[90, 160]]], ["f", "b", [[270, 160]]]],
      "tray": [{"type": "overflow", "n": 2}]})";
    Level good;
    std::vector<std::string> errors;
    REQUIRE(ParseLevel(shop, good, errors));
    REQUIRE(LookProblems(good).empty());
    auto moved = [&](int node, Vec2 at) {
      Level level = good;
      level.nodes[static_cast<size_t>(node)].at = at;
      return LookProblems(level);
    };
    CHECK(Has(moved(1, {240.0f, 30.0f}), "node f is under the bell"));
    CHECK(Has(moved(1, {50.0f, 60.0f}), "node f is under the jar of spout s"));
    CHECK(Has(moved(1, {20.0f, 60.0f}), "node f is under the slate of spout s"));
    CHECK(Has(moved(0, {80.0f, 40.0f}), "the jar of spout s is off the board"));
    CHECK(Has(moved(0, {50.0f, 80.0f}), "the slate of spout s is off the board"));
    CHECK(Has(moved(3, {270.0f, 440.0f}), "bottle b isn't on the counter"));
    // A second jar on top of the first.
    Level two = good;
    two.nodes.push_back(two.nodes[0]);
    two.nodes.back().id = "t";
    two.nodes.back().at = {100.0f, 120.0f};
    CHECK(Has(LookProblems(two), "the jar of spout t overlaps the jar of spout s"));
    Level unordered = good;
    unordered.nodes[3].order.clear();
    CHECK(Has(LookProblems(unordered), "bottle b has no order"));
  }

  TEST_CASE("a shop's jars: beside the tap's way, the slate on the far side, sized by the drops") {
    auto jar_of = [](const std::string& spout, const std::string& pipe, const std::string& shelves) {
      static Level level;  // JarAt reads the level's nodes and pipes
      std::vector<std::string> errors;
      const std::string text = R"({"id": "t-1", "name": "Jar", "shelves": [)" + shelves + R"(],
        "nodes": [{"id": "s", "kind": "spout", )" +
                               spout + R"(, "beads": "B*6", "beat": 4},
                  {"id": "c", "kind": "cup", "at": [180, 480], "want": "B", "count": 6, "order": "Baker"}],
        "pipes": [["s", "c")" + pipe +
                               R"(]]})";
      REQUIRE(ParseLevel(text, level, errors));
      return apothecary::JarAt(level, 0);
    };
    // Across to the right: the node inside the jar's tap side, the copper from the spout's edge.
    apothecary::Jar jar = jar_of(R"("at": [80, 80])", ", [[180, 80]]", "90");
    CHECK((!jar.down && jar.side == 1.0f && jar.at == Vec2{80.0f, 80.0f}));
    CHECK((jar.body.min == Vec2{40.0f, 30.0f} && jar.body.max == Vec2{92.0f, 96.0f}));
    CHECK((jar.whole.min == Vec2{40.0f, 16.0f} && jar.whole.max == Vec2{104.0f, 96.0f}));
    CHECK((jar.slate.min == Vec2{12.0f, 44.0f} && jar.slate.max == Vec2{36.0f, 74.0f}));
    CHECK((jar.feet == 96.0f && jar.on_shelf && jar.copper == 22.0f));
    CHECK(!jar_of(R"("at": [80, 80])", ", [[180, 80]]", "60").on_shelf);
    // Across to the left: mirrored.
    jar = jar_of(R"("at": [280, 80])", ", [[180, 80]]", "90");
    CHECK((jar.side == -1.0f && jar.body.min.x == 268.0f && jar.body.max.x == 320.0f));
    CHECK((jar.slate.min.x == 324.0f && jar.slate.max.x == 348.0f));
    // Down: the jar beside the node (left, room allowing), high enough for its knob, the stem across.
    jar = jar_of(R"("at": [180, 60])", "", "90");
    CHECK((jar.down && jar.side == 1.0f && jar.at == Vec2{144.0f, 66.0f} && jar.copper == 13.0f));
    CHECK((jar.slate.min.x == 76.0f && jar.whole.max.x == 186.0f));
    jar = jar_of(R"("at": [60, 40])", "", "90");
    CHECK((jar.down && jar.side == -1.0f && jar.at == Vec2{96.0f, 66.0f}));
    CHECK((jar.slate.min.x == 140.0f && jar.slate.max.x == 164.0f));
    // The slate: rows of ten in fives, growing with the drops.
    CHECK((apothecary::SlateSize(6) == Vec2{24.0f, 30.0f} && apothecary::SlateSize(11) == Vec2{34.0f, 45.0f}));
    const apothecary::Box slate{{10.0f, 20.0f}, {44.0f, 65.0f}};
    CHECK((apothecary::ChalkAt(slate, 0).top == Vec2{15.0f, 26.0f} && !apothecary::ChalkAt(slate, 0).strike));
    const apothecary::ChalkMark five = apothecary::ChalkAt(slate, 4);
    CHECK((five.strike && five.top == Vec2{13.0f, 26.0f} && five.strike_to == 26.0f));
    CHECK(apothecary::ChalkAt(slate, 5).top == Vec2{28.0f, 26.0f});
    CHECK(apothecary::ChalkAt(slate, 10).top == Vec2{15.0f, 41.0f});
    // The bell hangs left of the cork board; none without orders.
    const apothecary::Box bell = apothecary::BellBox(2);
    CHECK((bell.min == Vec2{221.0f, 1.0f} && bell.max == Vec2{263.0f, 48.0f}));
    CHECK(apothecary::BellBox(0).Empty());
  }

  TEST_CASE("a shop's look: slips on the cork board, the counter under the bottles") {
    const std::string shop = R"({"id": "t-1", "name": "Shop", "shelves": [60],
      "nodes": [{"id": "s", "kind": "spout", "at": [80, 80], "beads": "B*3", "beat": 4},
                {"id": "f", "kind": "fork", "at": [180, 160], "socket": true},
                {"id": "a", "kind": "cup", "at": [90, 480], "want": "B", "count": 2, "order": "Mrs Hale"},
                {"id": "b", "kind": "cup", "at": [270, 450], "want": "B", "count": 1, "order": "Baker"}],
      "pipes": [["s", "f", [[180, 80]]], ["f", "a", [[90, 160]]], ["f", "b", [[270, 160]]]],
      "tray": [{"type": "overflow", "n": 2}]})";
    Level level;
    std::vector<std::string> errors;
    REQUIRE(ParseLevel(shop, level, errors));
    const std::vector<apothecary::Order> orders = apothecary::Orders(level);
    REQUIRE(orders.size() == 2);
    CHECK((orders[0].node == 2 && orders[0].slot == -1 && orders[0].name == "Mrs Hale" && orders[0].count == 2));
    CHECK(orders[1].name == "Baker");
    // Two slips: 42 apart, 38 wide, 5 of cork round them; pinned to the wall's top right.
    const apothecary::Box board = apothecary::CorkBoard(2);
    CHECK((board.min == Vec2{266.0f, 2.0f} && board.max == Vec2{356.0f, 74.0f}));
    CHECK((apothecary::SlipPin(2, 0) == Vec2{290.0f, 8.0f} && apothecary::SlipPin(2, 1) == Vec2{332.0f, 8.0f}));
    CHECK(apothecary::CorkBoard(0).Empty());
    // More than four share a four-slip board.
    CHECK(apothecary::CorkBoard(6).min.x == apothecary::CorkBoard(4).min.x);
    CHECK(apothecary::SlipPin(6, 5).x == apothecary::SlipPin(4, 3).x);
    // The counter's top: the lowest bottle's feet on its face (the cup at 480: feet at 532, 10 down it).
    CHECK(apothecary::CounterTop(level) == 480.0f + apothecary::kBottleBase - apothecary::kBottleLip);
  }

  TEST_CASE("a shop's rack: a lazy susan on the counter, its bottles round the ring, a clock under it") {
    const std::string shop = R"({"id": "t-1", "name": "Rack", "shelves": [90, 226, 362],
      "nodes": [{"id": "s", "kind": "spout", "at": [80, 80], "beads": "B*8", "beat": 2},
                {"id": "r", "kind": "rack", "at": [180, 388], "period": 4,
                 "slots": [{"want": "B", "count": 4, "order": "Mrs Hale"}, {"want": "B", "count": 2, "order": "Baker"},
                           {"want": "B", "count": 2, "order": "Dr Moss"}]}],
      "pipes": [["s", "r", [[180, 80]]]]})";
    Level good;
    std::vector<std::string> errors;
    REQUIRE(ParseLevel(shop, good, errors));
    REQUIRE(LookProblems(good).empty());
    const Node& rack = good.nodes[1];
    CHECK(apothecary::CounterTop(good) == 467.0f);

    // The mock's three: the front one under the tap, the others back at the sides, smaller.
    const apothecary::RackSpot front = apothecary::RackBottle(rack, 0, 0.0f);
    CHECK((front.mouth == Vec2{180.0f, 438.0f} && front.base == Vec2{180.0f, 490.0f}));
    CHECK((front.scale == doctest::Approx(1.0f) && front.depth == doctest::Approx(1.0f)));
    CHECK(front.base.y == rack.at.y + apothecary::kRackBase);
    const apothecary::RackSpot left = apothecary::RackBottle(rack, 1, 0.0f);
    const apothecary::RackSpot right = apothecary::RackBottle(rack, 2, 0.0f);
    CHECK((left.base.x == doctest::Approx(123.708f) && right.base.x == doctest::Approx(236.292f)));
    CHECK((left.scale == doctest::Approx(0.82f) && left.base.y == doctest::Approx(477.28f)));
    CHECK((left.depth < front.depth && right.depth < front.depth));
    // One step brings the next bottle to the front; half a step has the two either side of it.
    const apothecary::RackSpot next = apothecary::RackBottle(rack, 1, 1.0f);
    CHECK((next.mouth.x == doctest::Approx(front.mouth.x) && next.mouth.y == doctest::Approx(front.mouth.y)));
    const apothecary::RackSpot going = apothecary::RackBottle(rack, 0, 0.5f),
                               coming = apothecary::RackBottle(rack, 1, 0.5f);
    CHECK((going.depth == doctest::Approx(coming.depth) && going.base.x > rack.at.x && coming.base.x < rack.at.x));
    // Four: the one at the back, the smallest, right behind the front one.
    Level four = good;
    four.nodes[1].slots.push_back(four.nodes[1].slots[1]);
    const apothecary::RackSpot back = apothecary::RackBottle(four.nodes[1], 2, 0.0f);
    CHECK((back.base.x == doctest::Approx(180.0f) && back.scale == doctest::Approx(0.76f) && back.depth < -0.99f));
    CHECK(LookProblems(four).empty());

    const apothecary::Box table = apothecary::RackBox(rack);
    CHECK((table.min == Vec2{84.0f, 428.0f} && table.max == Vec2{276.0f, 508.0f}));
    CHECK(apothecary::ClockAt(rack) == Vec2{180.0f, 540.0f});

    // What's wrong with a rack on a shop wall.
    Level changed = good;
    changed.nodes[1].slots.resize(5, changed.nodes[1].slots[1]);
    CHECK(Has(LookProblems(changed), "rack r needs 2 to 4 bottles"));
    changed = good;
    changed.nodes[1].slots[1].order.clear();
    CHECK(Has(LookProblems(changed), "rack r's bottle 2 has no order"));
    changed = good;
    changed.nodes[1].at.x = 60.0f;
    CHECK(Has(LookProblems(changed), "rack r's turntable is off the board"));
    changed = good;
    changed.pipes[0].via = {{80.0f, 388.0f}};
    CHECK(Has(LookProblems(changed), "pipe s -> r doesn't come down into the rack's tap"));
    // A bottle on a lower counter, in the turntable's way, its tag behind the clock.
    changed = good;
    Node cup;
    cup.id = "c";
    cup.kind = NodeKind::Cup;
    cup.at = {190.0f, 478.0f};
    cup.order = "Baker";
    changed.nodes.push_back(cup);
    std::vector<std::string> problems = LookProblems(changed);
    CHECK(Has(problems, "rack r isn't on the counter"));
    CHECK(Has(problems, "rack r's turntable runs into c"));
    CHECK(Has(problems, "rack r's clock is over bottle c's tag"));
    Node sink;
    sink.id = "d";
    sink.kind = NodeKind::Drain;
    sink.at = {250.0f, 470.0f};
    changed = good;
    changed.nodes.push_back(sink);
    CHECK(Has(LookProblems(changed), "rack r's turntable runs into d"));
  }

  TEST_CASE("every level's look is right: nodes on the board, shop walls straight") {
    for (const World& world : LoadAll()) {
      for (const Level& level : world.levels) {
        INFO("level " << level.id);
        CHECK(level.look.height >= 600.0f);
        for (const std::string& problem : LookProblems(level)) CHECK_MESSAGE(false, problem);
      }
    }
  }

  TEST_CASE("generated levels are the same on every device") {
    // Fingerprints of a week of dailies and three Lab levels. The daily puzzle is "the same for everyone":
    // a compiler or platform that builds a different level from the same seed fails here (run these
    // tests on a phone too: CLAUDE.md). Change them only on purpose (a new generator = new puzzles).
    struct Print {
      int seed;
      const char* lab;  // null = a daily (seed = the date)
      const char* name;
      size_t nodes;
      int pieces, ticks, solutions;
    };
    // Rules v3 (open sockets, 17 October 2026) made these.
    const Print prints[] = {
        {20261012, nullptr, "Winding Press", 6, 1, 86, 14},  {20261013, nullptr, "Sleepy Loom", 5, 1, 86, 3},
        {20261014, nullptr, "Hollow Channel", 8, 2, 115, 6}, {20261015, nullptr, "Glass Loom", 6, 2, 95, 2},
        {20261016, nullptr, "Sleepy Valve", 6, 2, 90, 1},    {20261017, nullptr, "Copper Spring", 7, 3, 116, 2},
        {20261018, nullptr, "Twin Loom", 8, 3, 131, 4},      {7, "easy", "Copper Canal", 5, 1, 100, 3},
        {7, "medium", "Velvet Canal", 6, 2, 116, 1},         {7, "hard", "Crooked Valve", 8, 3, 123, 2},
    };
    for (const Print& print : prints) {
      const Level level = print.lab ? GenerateLab(print.seed, print.lab) : GenerateDaily(print.seed);
      INFO(level.id << " " << level.name);
      CHECK(level.name == print.name);
      CHECK(level.nodes.size() == print.nodes);
      CHECK(level.pars.pieces == print.pieces);
      CHECK(level.pars.ticks == print.ticks);
      CHECK(level.pars.solutions == print.solutions);
    }
  }

  TEST_CASE("level ids are unique and worlds are numbered") {
    const std::vector<World> worlds = LoadAll();
    std::vector<std::string> ids;
    for (size_t w = 0; w < worlds.size(); ++w) {
      CHECK(worlds[w].number == static_cast<int>(w) + 1);
      for (const Level& level : worlds[w].levels) ids.push_back(level.id);
    }
    std::sort(ids.begin(), ids.end());
    CHECK(std::adjacent_find(ids.begin(), ids.end()) == ids.end());
  }

  TEST_CASE("generated levels: deterministic, valid, solvable, harder later in the week") {
    const auto start = std::chrono::steady_clock::now();
    for (int date : {20261012, 20261013, 20261014, 20261015, 20261016, 20261017, 20261018}) {
      const Level a = GenerateDaily(date);
      const Level b = GenerateDaily(date);
      INFO("daily " << date);
      REQUIRE(!a.nodes.empty());
      CHECK(a.id == b.id);
      CHECK(a.nodes.size() == b.nodes.size());
      CHECK(a.pars.ticks == b.pars.ticks);
      CHECK(a.pars.pieces == b.pars.pieces);
      Level copy = a;
      CHECK(copy.Finalize().empty());
      const SolveReport report = Solve(copy);
      CHECK(report.solutions > 0);
      CHECK(report.pars.pieces == a.pars.pieces);
      CHECK(Simulate(copy, copy.pars.lean).solved);
    }
    CHECK(DailyWeekday(20261012) == 0);  // a Monday
    CHECK(DailyWeekday(20261018) == 6);  // a Sunday
    for (const char* difficulty : {"easy", "medium", "hard"}) {
      for (int64_t seed = 1; seed <= 6; ++seed) {
        Level level = GenerateLab(seed, difficulty);
        INFO("lab " << difficulty << " " << seed);
        REQUIRE(!level.nodes.empty());
        CHECK(level.Finalize().empty());
        CHECK(Simulate(level, level.pars.lean).solved);
      }
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    MESSAGE("generated 25 levels in " << ms << " ms");
  }
}
