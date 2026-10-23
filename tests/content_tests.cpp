// The game's content: every shipped level (assets/levels) is valid, solvable and matches the pars
// file; generated levels (daily, Lab) are deterministic, valid and solvable.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "../src/apothecary_look.h"
#include "../src/canal_look.h"
#include "../src/generator.h"
#include "../src/level_io.h"
#include "../src/sigil_look.h"
#include "../src/solver.h"
#include "doctest.h"
#include "test_levels.h"

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

// A shipped level by id, with its pars.
Level Shipped(const std::string& id) {
  for (const World& world : LoadAll()) {
    for (const Level& level : world.levels) {
      if (level.id == id) return level;
    }
  }
  FAIL("no level " << id);
  return {};
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
                 a.order == b.order && a.district == b.district && a.twin == b.twin));
          CHECK((a.slots == b.slots && a.period == b.period && a.first == b.first && a.gears == b.gears &&
                 a.rods == b.rods && a.open == b.open));
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
    // A grand shop two screens tall pins its board at the top of the second, over the counter; the bell
    // and the slips go with it, and so does the room the machine must leave them.
    CHECK(apothecary::CorkTop(level) == 2.0f);
    Level grand = level;
    grand.look.height = 1200.0f;
    const float top = apothecary::CorkTop(grand);
    CHECK(top == 602.0f);
    CHECK((apothecary::CorkBoard(2, top).min == Vec2{266.0f, 602.0f} && apothecary::CorkBoard(2, top).max.y == 674.0f));
    CHECK(apothecary::SlipPin(2, 1, top) == Vec2{332.0f, 608.0f});
    CHECK(apothecary::BellBox(2, top).min == Vec2{221.0f, 601.0f});
    CHECK(apothecary::BellHang(2, top).y == apothecary::BellHang(2).y + 600.0f);
    grand.nodes[1].at = {300.0f, 640.0f};
    CHECK(Has(LookProblems(grand), "node f is under the cork board"));
    grand.nodes[1].at = {300.0f, 40.0f};
    CHECK(!Has(LookProblems(grand), "node f is under the cork board"));
    // The counter's top: the lowest bottle's feet on its face (the cup at 480: feet at 532, 10 down it).
    CHECK(apothecary::CounterTop(level) == 480.0f + apothecary::kBottleBase - apothecary::kBottleLip);
  }

  TEST_CASE("a shop's paid moment: cork, label, bell, stamp, in real seconds") {
    using apothecary::PaidAt;
    // Nothing yet, then the cork (0..0.35 s), the label (0.4..0.8 s), the stamp (1.0..1.3 s).
    CHECK((PaidAt(0.0f, false).cork == 0.0f && PaidAt(0.0f, false).label == 0.0f && PaidAt(0.0f, false).stamp == 0.0f));
    CHECK(PaidAt(0.35f, false).cork == doctest::Approx(1.0f));
    CHECK((PaidAt(0.6f, false).label == doctest::Approx(0.5f) && PaidAt(0.6f, false).stamp == 0.0f));
    CHECK((PaidAt(1.15f, false).stamp == doctest::Approx(0.5f) && PaidAt(1.15f, false).label == 1.0f));
    CHECK(PaidAt(apothecary::kPaidLength, false).stamp == 1.0f);
    // Reduced motion: the end at once.
    CHECK((PaidAt(0.0f, true).cork == 1.0f && PaidAt(0.0f, true).label == 1.0f && PaidAt(0.0f, true).stamp == 1.0f));
    CHECK((apothecary::EaseOut(0.0f) == 0.0f && apothecary::EaseOut(1.0f) == 1.0f && apothecary::EaseOut(0.5f) > 0.5f));

    // The bell: still before it rings, swinging and dying away after, quiet after 2.4 s.
    CHECK(apothecary::BellAt(-0.1f, false).swing == 0.0f);
    CHECK(std::fabs(apothecary::BellAt(0.13f, false).swing) > 0.2f);
    CHECK(std::fabs(apothecary::BellAt(1.6f, false).swing) < 0.04f);
    CHECK((apothecary::BellAt(2.5f, false).swing == 0.0f && apothecary::BellAt(0.1f, true).swing == 0.0f));
    CHECK((apothecary::BellAt(0.0f, false).wave_alpha == 1.0f && apothecary::BellAt(1.0f, false).wave_alpha == 0.0f));
    CHECK(apothecary::BellAt(0.5f, false).wave > apothecary::BellAt(0.1f, false).wave);
  }

  TEST_CASE("a shop's failures: a spill runs over and pools, a short order's ghost fills in") {
    using apothecary::SpillAt;
    CHECK((SpillAt(0.0f, false).stream == 0.0f && SpillAt(0.0f, false).puddle == 0.0f));
    CHECK((SpillAt(0.45f, false).stream == doctest::Approx(0.5f) && SpillAt(0.3f, false).drip_b == 0.0f));
    CHECK(SpillAt(0.25f, false).cork_off == doctest::Approx(0.5f));
    const apothecary::SpillMoment done = SpillAt(apothecary::kSpillLength, false);
    CHECK((done.stream == 1.0f && done.puddle == 1.0f && done.drip_a == 1.0f && done.drip_b == doctest::Approx(0.9f)));
    CHECK(SpillAt(0.0f, true).puddle == 1.0f);

    CHECK((apothecary::ShortAt(0.75f, false).ghost == doctest::Approx(0.5f) &&
           apothecary::ShortAt(0.5f, false).pill == 1.0f));
    CHECK(apothecary::ShortAt(0.0f, true).ghost == 1.0f);
    CHECK((apothecary::UnpaidAt(0.0f, false) == 0.0f && apothecary::UnpaidAt(1.2f, false) == 1.0f));
    CHECK(apothecary::UnpaidAt(0.0f, true) == 1.0f);
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

  TEST_CASE("a canal map: harbours for districts, canals straight with round bends, rods along the streets") {
    // World 3's grammar, on 3-6 The Mill Gate.
    const Level level = MakeLevel(kMillGateCanals);
    REQUIRE(canal::IsCanalMap(level));
    const std::vector<canal::District> districts = canal::Districts(level);
    REQUIRE(districts.size() == 2);
    CHECK((districts[0].node == NodeOf(level, "ca") && districts[0].name == "Lantern Row"));
    CHECK(districts[1].name == "Old Quay");
    const int m = NodeOf(level, "m");
    CHECK(RodPoints(level, m, 0) == std::vector<Vec2>{{180, 360}, {250, 360}, {250, 270}});
    CHECK(LookProblems(level).empty());
    Level copy;
    std::vector<std::string> errors;
    REQUIRE(ParseLevel(WriteLevel(level), copy, errors));
    CHECK(copy.nodes[static_cast<size_t>(NodeOf(level, "cb"))].district == "Old Quay");
    CHECK(copy.nodes[static_cast<size_t>(m)].rods == level.nodes[static_cast<size_t>(m)].rods);

    // Each broken variant is its one problem.
    auto problems = [](const std::string& from, const std::string& to) {
      std::string json = kMillGateCanals;
      REQUIRE(json.find(from) != std::string::npos);
      json.replace(json.find(from), from.size(), to);
      return LookProblems(MakeLevel(json));
    };
    using List = std::vector<std::string>;
    CHECK(problems(R"(, "district": "Old Quay")", "") == List{"harbour cb has no district"});
    CHECK(problems("Old Quay", "Old Quay by the Locks") == List{"harbour cb's district name has more than 14 letters"});
    CHECK(problems("Old Quay", "Lantern Row") == List{"harbours ca and cb both serve Lantern Row"});
    CHECK(problems("[60, 420]]", "[70, 420]]") == List{"pipe m -> ca runs at a slant (segment 2)"});
    CHECK(Has(problems("[[180, 420], [60, 420]]", "[[180, 380], [60, 380]]"),
              "canal m -> ca has no room to bend (segment 0)"));
    CHECK(problems(R"(["pb", "x", [[60, 270]]])", R"(["pb", "x", [[100, 150], [100, 270]]])") ==
          List{"node pb isn't on a straight stretch"});
    {
      std::string json = kMillGateCanals;  // Lantern Row's harbour beside Old Quay's canal
      json.replace(json.find(R"("at": [60, 520])"), 15, R"("at": [280, 520])");
      json.replace(json.find("[60, 420]]"), 10, "[280, 420]]");
      CHECK(Has(LookProblems(MakeLevel(json)), "canal g -> cb runs along canal m -> ca"));
      CHECK(Has(LookProblems(MakeLevel(json)), "harbours ca and cb overlap"));
    }
    CHECK(Has(problems("[[180, 420], [60, 420]]", "[[180, 420], [330, 420], [330, 560], [60, 560]]"),
              "canal g -> cb crosses canal m -> ca outside a crossing"));
    // Room for the city's buildings.
    CHECK(problems(R"("at": [60, 520])", R"("at": [60, 540])") == List{"harbour ca's basin or name is off the board"});
    CHECK(problems("[[180, 420], [60, 420]]", "[[180, 394], [60, 394]]") ==
          List{"the mill m's house sits on canal m -> ca"});
    CHECK(problems(R"([["g", [[250, 360]]]])", R"(["g"])") == List{"the rod m -> g runs at a slant (segment 0)"});
    CHECK(problems("[[250, 360]]", "[[365, 360], [365, 300], [250, 300]]") == List{"the rod m -> g is off the board"});
    CHECK(problems("[[250, 360]]", "[[180, 400], [250, 400]]") == List{"the rod m -> g runs along canal m -> ca"});
    // These break more than one rule at once.
    CHECK(Has(problems("[[180, 120]]", "[[240, 270]]"), "canal pa -> x runs through node g"));
    CHECK(Has(problems("[[300, 120]]", "[[300, 120], [340, 120]]"), "canal sa -> pa doubles back (segment 2)"));

    // Without districts it's no canal map: none of this is checked.
    std::string plain = kMillGateCanals;
    for (const std::string district : {R"(, "district": "Lantern Row")", R"(, "district": "Old Quay")"})
      plain.replace(plain.find(district), district.size(), "");
    plain.replace(plain.find(R"([["g", [[250, 360]]]])"), 21, R"(["g"])");
    const Level old = MakeLevel(plain);
    CHECK(!canal::IsCanalMap(old));
    CHECK(LookProblems(old).empty());
  }

  TEST_CASE("a sigil: junctions on its points and lines, grooves straight or along its rings") {
    // World 4's grammar, on 4-7 The Twin Seals.
    const Level level = MakeLevel(kTwinSeals);
    REQUIRE(sigil::IsSigil(level));
    CHECK(level.look.sigil == Sigil{{180, 290}, {150}});
    // Ring point k is k * 15 degrees clockwise from +x (y down): the triangle's corners.
    CHECK(std::fabs(SigilPoint(level.look.sigil, 0, 14).x - 50.1f) < 0.1f);
    CHECK(std::fabs(SigilPoint(level.look.sigil, 0, 14).y - 215.0f) < 0.01f);
    CHECK(std::fabs(SigilPoint(level.look.sigil, 0, 6).y - 440.0f) < 0.01f);
    CHECK(LookProblems(level).empty());
    Level copy;
    std::vector<std::string> errors;
    REQUIRE(ParseLevel(WriteLevel(level), copy, errors));
    CHECK(copy.look == level.look);

    // An arc: sb's pipe comes round the ring from the top point to p1, 60 degrees the shorter way.
    auto with = [](const std::string& from, const std::string& to) {
      std::string json = kTwinSeals;
      REQUIRE(json.find(from) != std::string::npos);
      json.replace(json.find(from), from.size(), to);
      return json;
    };
    const Level arc = MakeLevel(with(R"(["sb", "p1"])", R"(["sb", "p1", [[180, 140]], {"arc": 1}])"));
    CHECK(LookProblems(arc).empty());
    const Pipe& pipe = arc.pipes[0];
    CHECK(pipe.arc_segment == 1);
    // 16 steps of 3.75 degrees, each a chord of 2 r sin(1.875 degrees); the straight part; the nodes' edges.
    const double chords = 16.0 * 2.0 * 150.0 * std::sin(1.875 * 3.14159265358979323846 / 180.0);
    const double length = std::sqrt(130.0 * 130.0 + 80.0 * 80.0) + chords - 22.0 - 5.0;
    CHECK(std::fabs(pipe.length - length) < 0.1);  // the rules' drawn length is a float sum
    CHECK(pipe.cells == static_cast<int>(std::floor(length / 10.0 + 0.5)));
    CHECK(pipe.points.size() == 2 + 15 + 1);  // the edges, the arc's inner points and its start (the bend)
    const std::vector<Vec2> groove = sigil::GroovePoints(arc, pipe);
    REQUIRE(groove.size() == 18);
    CHECK(groove.front() == Vec2{50, 60});
    CHECK(groove[1] == Vec2{180, 140});
    CHECK(groove.back() == Vec2{50, 215});
    for (size_t k = 1; k + 1 < groove.size(); ++k) {
      const float r = std::sqrt((groove[k].x - 180) * (groove[k].x - 180) + (groove[k].y - 290) * (groove[k].y - 290));
      CHECK(std::fabs(r - 150.0f) < 0.01f);
      CHECK(groove[k].x <= 180.0f);  // the shorter way: over the top left, not round the right
    }
    Level arc_copy;
    REQUIRE(ParseLevel(WriteLevel(arc), arc_copy, errors));
    CHECK(arc_copy.pipes[0].arc_segment == 1);
    CHECK(arc_copy.pipes[0].cells == pipe.cells);

    // Arcs the rules refuse.
    auto refused = [&](const std::string& json, const std::string& problem) {
      Level bad;
      std::vector<std::string> found;
      CHECK(!ParseLevel(json, bad, found));
      bool has = false;
      for (const std::string& error : found) has = has || error.find(problem) != std::string::npos;
      INFO(problem);
      CHECK(has);
    };
    refused(with(R"(["sb", "p1"])", R"(["sb", "p1", [], {"arc": 0}])"),
            "the arc on sb -> p1 needs both ends on points of one ring");
    refused(with(R"(["sb", "p1"])", R"(["sb", "p1", [[309.9, 365]], {"arc": 1}])"),
            "the arc on sb -> p1 needs both ends on points of one ring");  // half a ring apart
    refused(with(R"(["sb", "p1"])", R"(["sb", "p1", [[180, 140]], {"arc": 1, "drip": 1}])"),
            "the arc on sb -> p1 needs a segment of the pipe without a coil or a drip");

    // Each broken variant is its one problem.
    auto problems = [&](const std::string& from, const std::string& to) {
      return LookProblems(MakeLevel(with(from, to)));
    };
    using List = std::vector<std::string>;
    CHECK(problems(R"("at": [330, 430])", R"("at": [270, 400])") == List{"node cr is inside the sigil"});
    CHECK(problems(R"("at": [180, 440], "socket")", R"("at": [180, 442], "socket")") ==
          List{"node x1 isn't on the sigil's points or lines", "node x2 isn't on the sigil's points or lines",
               "node m isn't on the sigil's points or lines"});
    CHECK(problems(R"("rings": [150])", R"("rings": [150, 200])") ==
          List{"the sigil is off the board", "node sr is inside the sigil"});
    CHECK(problems(R"(["sr", "x1"])", R"(["sr", "x1", [[20, 380], [140, 380], [140, 330]]])") ==
          List{"groove sr -> x1 crosses groove x1 -> x2 outside a crossing",  // its bend on it, then along it
               "groove sr -> x1 crosses groove x1 -> m outside a crossing"});
    CHECK(Has(problems(R"(["sb", "p1"])", R"(["sb", "p1", [[310, 60]]])"), "groove sb -> p1 runs through node sa"));
    CHECK(problems(R"("at": [20, 330])", R"("at": [180, 215])") ==
          List{"node sr is inside the sigil", "the bar of twin pair p1 + p2 runs over node sr"});

    // Without a sigil none of this is checked.
    const Level plain = MakeLevel(with(R"(, "sigil": {"at": [180, 290], "rings": [150]})", ""));
    CHECK(!sigil::IsSigil(plain));
    CHECK(LookProblems(plain).empty());
  }

  TEST_CASE("the canal city round 3-6: locks, harbours, the mill's house, the gate's lamp, rooftops") {
    const Level level = MakeLevel(kMillGateCanals);
    auto near = [](Vec2 a, Vec2 b) { return std::fabs(a.x - b.x) < 0.01f && std::fabs(a.y - b.y) < 0.01f; };
    auto same = [&](const canal::Box& a, canal::Box b) { return near(a.min, b.min) && near(a.max, b.max); };

    // The blue lock at (60, 30), its canal running down: the chamber 30 behind to 18 ahead, the stub up off the top.
    const canal::Lock lock = canal::LockAt(level, NodeOf(level, "sb"));
    CHECK(near(lock.frame.u, {0, 1}));
    CHECK(near(lock.frame.w, {-1, 0}));
    CHECK(same(lock.chamber, {{45, 0}, {75, 48}}));
    CHECK(lock.stub == std::vector<Vec2>{{60, -100}, {60, 0}});

    // Lantern Row's harbour at (60, 520): a basin 72 deep, its name under it, the first berth on the left.
    const canal::Harbour harbour = canal::HarbourAt(level, NodeOf(level, "ca"));
    CHECK(harbour.rows == 1);
    CHECK(same(harbour.basin, {{14, 508}, {106, 580}}));
    CHECK(same(harbour.pill, {{14, 580}, {106, 598}}));
    CHECK(near(harbour.Berth(0), {27.5f, 545.5f}));
    CHECK(near(harbour.Berth(5), {92.5f, 545.5f}));

    // The mill at (180, 360): its rod runs right, so its house stands on the left bank.
    const canal::Mill mill = canal::MillAt(level, NodeOf(level, "m"));
    CHECK(same(mill.house, {{124, 338}, {160, 382}}));
    // The gate at (250, 270) on a canal running right; its rod comes up from below, so its lamp is below.
    const canal::Gate gate = canal::GateAt(level, NodeOf(level, "g"));
    CHECK(near(gate.lamp, {262, 298}));

    // The city: the same every time, a park on the board, no rooftop on a canal or a building.
    const canal::City city = canal::MakeCity(level);
    CHECK(city.roofs.size() > 100);
    CHECK(city.trees.size() == 16);
    CHECK(city.lamps.size() > 10);
    CHECK(city.park.max.x - city.park.min.x == doctest::Approx(70.0f));
    const canal::City again = canal::MakeCity(level);
    REQUIRE(again.roofs.size() == city.roofs.size());
    CHECK(near(again.roofs[17].box.min, city.roofs[17].box.min));
    const std::vector<std::vector<Vec2>> canals = canal::CityCanals(level);
    CHECK(canals[0] == std::vector<Vec2>{{60, -100}, {60, 30}, {60, 150}});  // the stub, then sb -> pb
    const std::vector<canal::Box> keep = canal::KeepOuts(level);
    for (const canal::Roof& roof : city.roofs) {
      for (const std::vector<Vec2>& canal : canals) {
        for (size_t k = 0; k + 1 < canal.size(); ++k) {
          const canal::Box clear{
              {std::min(canal[k].x, canal[k + 1].x) - 14, std::min(canal[k].y, canal[k + 1].y) - 14},
              {std::max(canal[k].x, canal[k + 1].x) + 14, std::max(canal[k].y, canal[k + 1].y) + 14}};
          CHECK(!roof.box.Overlaps(clear));
        }
      }
      for (const canal::Box& box : keep) CHECK(!roof.box.Overlaps(box));
      CHECK(!roof.box.Overlaps(city.park));
    }
    for (const Vec2 lamp : city.lamps) {
      for (const Node& node : level.nodes) CHECK(std::hypot(lamp.x - node.at.x, lamp.y - node.at.y) >= 22.0f);
    }
    // Another level, another city.
    std::string other = kMillGateCanals;
    other.replace(other.find(R"("id": "t-3-6")"), 13, R"("id": "t-3-7")");
    CHECK(!near(canal::MakeCity(MakeLevel(other)).roofs[17].box.min, city.roofs[17].box.min));
  }

  TEST_CASE("a mill's rod pulses from the mill to the gate in one tick; the wheel turns a notch a drop") {
    const Level level = MakeLevel(kMillGateCanals);
    const std::vector<Vec2> rod = RodPoints(level, NodeOf(level, "m"), 0);  // (180,360) -> (250,360) -> (250,270)
    auto near = [](std::optional<Vec2> a, Vec2 b) {
      return a && std::fabs(a->x - b.x) < 0.01f && std::fabs(a->y - b.y) < 0.01f;
    };
    CHECK(!canal::PulseAt(rod, -0.1f));
    CHECK(!canal::PulseAt(rod, 1.1f));
    CHECK(near(canal::PulseAt(rod, 0.0f), {180, 360}));
    CHECK(near(canal::PulseAt(rod, 70.0f / 160.0f), {250, 360}));    // the bend: 70 of the rod's 160
    CHECK(near(canal::PulseAt(rod, 1.0f), {250, 270}));              // the gate
    CHECK(canal::PulseAt(rod, 0.5f)->y == doctest::Approx(350.0f));  // 80 along: 10 up the second run
    CHECK(canal::PulseAt({{0, 0}}, 0.5f) == std::nullopt);           // no rod, no pulse

    CHECK(canal::WheelAngle(0, 0.0f) == doctest::Approx(0.0f));
    CHECK(canal::WheelAngle(1, 0.0f) == doctest::Approx(3.14159265f / 3.0f));
    CHECK(canal::WheelAngle(0, 0.5f) == doctest::Approx(3.14159265f / 6.0f));
    CHECK(canal::WheelAngle(2, 1.5f) == doctest::Approx(3.14159265f));  // inside is capped at 1: three notches
  }

  TEST_CASE("a jam's circle runs along the canals and rods, closed, through the nodes it joins") {
    const Level level = MakeLevel(kMillGateCanals);
    const int x = NodeOf(level, "x"), g = NodeOf(level, "g"), m = NodeOf(level, "m");
    // Every canal's middle line and every mill's rod, as segments.
    std::vector<std::pair<Vec2, Vec2>> lines;
    for (const Pipe& pipe : level.pipes) {
      const std::vector<Vec2> points = canal::CanalPoints(level, pipe);
      for (size_t k = 0; k + 1 < points.size(); ++k) lines.push_back({points[k], points[k + 1]});
    }
    const std::vector<Vec2> rod = RodPoints(level, m, 0);
    for (size_t k = 0; k + 1 < rod.size(); ++k) lines.push_back({rod[k], rod[k + 1]});
    auto distance_to = [&](Vec2 p) {
      float best = 1e9f;
      for (const auto& [a, b] : lines) {
        const float dx = b.x - a.x, dy = b.y - a.y;
        const float t = std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / (dx * dx + dy * dy), 0.0f, 1.0f);
        best = std::min(best, std::hypot(p.x - (a.x + t * dx), p.y - (a.y + t * dy)));
      }
      return best;
    };
    auto near_node = [&](const std::vector<Vec2>& loop, int node) {
      const Vec2 at = level.nodes[static_cast<size_t>(node)].at;
      float best = 1e9f;
      for (const Vec2 p : loop) best = std::min(best, std::hypot(p.x - at.x, p.y - at.y));
      return best;
    };

    // x -> g: the shut gate's mill comes in before it (x, m, g), the rod joins m and g.
    const std::vector<Vec2> loop = canal::JamLoop(level, {x, g}, -1);
    REQUIRE(loop.size() >= 4);
    CHECK(loop.front().x == doctest::Approx(loop.back().x));
    CHECK(loop.front().y == doctest::Approx(loop.back().y));
    for (const Vec2 p : loop) CHECK(distance_to(p) <= canal::kLoopInset + 0.5f);
    for (const int n : {x, m, g}) {
      INFO("node ", n, " is ", near_node(loop, n), " from the loop; ", loop.size(), " points");
      CHECK(near_node(loop, n) <= canal::kLoopInset + 0.5f);
    }

    // The lonely gate joins the circle as well; a single node is no circle.
    CHECK(!canal::JamLoop(level, {x}, g).empty());
    CHECK(canal::JamLoop(level, {x}, -1).empty());

    // A red lamp beside each stuck node, clear of every canal's water (half a canal, 14, from its middle line).
    for (const char* id : {"x", "g", "m", "pa", "pb"}) {
      const int n = NodeOf(level, id);
      const Vec2 lamp = canal::LampAt(level, n);
      CHECK(std::hypot(lamp.x - level.nodes[static_cast<size_t>(n)].at.x,
                       lamp.y - level.nodes[static_cast<size_t>(n)].at.y) ==
            doctest::Approx(canal::kLampOffset * std::sqrt(2.0f)));
      CHECK(distance_to(lamp) >= 14.0f);
    }
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

  TEST_CASE("every World 2 level is a shop: a shop wall, an order on every bottle") {
    for (const World& world : LoadAll()) {
      if (world.number != 2) continue;
      for (const Level& level : world.levels) {
        INFO("level " << level.id);
        CHECK(!level.look.shelves.empty());
        int bottles = 0;
        for (const Node& node : level.nodes) {
          if (node.kind == NodeKind::Cup) ++bottles;
          if (node.kind == NodeKind::Rack) bottles += static_cast<int>(node.slots.size());
        }
        CHECK(bottles > 0);
        CHECK(static_cast<int>(apothecary::Orders(level).size()) == bottles);
      }
    }
  }

  TEST_CASE("every World 3 level is a canal map: its own districts, and every run ends") {
    std::vector<std::string> districts;
    int canal_maps = 0;
    for (const World& world : LoadAll()) {
      if (world.number != 3) continue;
      for (const Level& level : world.levels) {
        INFO("level " << level.id);
        const bool grand = level.look.height > 600.0f;  // the finale, Rush Hour: the whole city on two screens
        CHECK(canal::IsCanalMap(level));
        ++canal_maps;
        // Each harbour serves a district of its own across the world (the map's neighbourhoods); the finale comes back
        // to the best-known ones.
        for (const canal::District& district : canal::Districts(level)) {
          if (grand) continue;
          INFO("district " << district.name);
          CHECK(std::find(districts.begin(), districts.end(), district.name) == districts.end());
          districts.push_back(district.name);
        }
        // A ring of canals could carry barges round forever (a run that ends only at the safety net, ~40 minutes
        // on screen): every placement the solver tries ends, failed or not, well before that.
        int longest = 0;
        ForEachPlacement(level, [&](const Placement& placement) {
          const Outcome outcome = Simulate(level, placement, false);
          CHECK(outcome.over);
          longest = std::max(longest, outcome.ticks);
          return true;
        });
        CHECK(longest < (grand ? 800 : 400));
      }
    }
    CHECK(canal_maps == 18);
  }

  TEST_CASE("a shipped canal map's city stays where it was (the map's canal quarter shares its rooftops)") {
    // Pinned from MakeCity before its rooftop pass became canal::Rooftops.
    const canal::City city = canal::MakeCity(Shipped("3-6"));
    REQUIRE(city.roofs.size() == 472);
    size_t windows = 0;
    for (const canal::Roof& roof : city.roofs) windows += roof.windows.size();
    CHECK(windows == 329);
    CHECK(city.lamps.size() == 20);
    CHECK(city.roofs[17].box.min.x == doctest::Approx(288.8289f));
    CHECK(city.roofs[17].box.min.y == doctest::Approx(-184.2526f));
    CHECK(city.roofs.back().box.max.x == doctest::Approx(432.2731f));
    CHECK(city.roofs.back().box.max.y == doctest::Approx(854.0090f));
    CHECK(city.roofs.back().tone == 1);
    CHECK(city.park.min.x == doctest::Approx(173.0f));
    CHECK(city.park.min.y == doctest::Approx(481.0f));
    CHECK(city.trees[3].r == doctest::Approx(10.9681f));
  }

  TEST_CASE("The Roundabout (3-8): barges meet coming round; a lock or a tank on the ring, and the ring empties") {
    const Level level = Shipped("3-8");
    const int m1 = NodeOf(level, "m1"), m2 = NodeOf(level, "m2");
    {
      Board board(level);
      const Run run = RunBoard(board);
      CHECK(run.outcome.failure == Failure::Mud);
      CHECK(run.outcome.fail_node == m2);
      CHECK(run.outcome.fail_tick == 39);
    }
    {
      Board board(level);
      REQUIRE(board.Place(PieceOf(board, PieceType::Turnstile), m2));
      const Run run = RunBoard(board);
      CHECK(run.outcome.solved);
      CHECK(run.outcome.ticks == 84);
    }
    {
      Board board(level);
      REQUIRE(board.Place(PieceOf(board, PieceType::TankShort), m1));
      const Run run = RunBoard(board);
      CHECK(run.outcome.solved);
      CHECK(run.outcome.ticks == 88);
    }
  }

  TEST_CASE("Two Mills (3-9): a fast blue shuts amber's gate for good; a tall tank on blue lets them take turns") {
    const Level level = Shipped("3-9");
    const int ga = NodeOf(level, "ga"), pb = NodeOf(level, "pb");
    {
      Board board(level);
      const Run run = RunBoard(board);
      CHECK(run.outcome.jammed);
      CHECK(run.outcome.fail_node == ga);
      CHECK(run.outcome.fail_tick == 67);
    }
    {
      Board board(level);
      REQUIRE(board.Place(PieceOf(board, PieceType::TankTall), pb));
      const Run run = RunBoard(board);
      CHECK(run.outcome.solved);
      CHECK(run.outcome.ticks == 100);
      CHECK(run.Count(EventType::Flip) == 12);
    }
  }

  TEST_CASE("Rush Hour (3-15): one bridge for two crossings; the other waits, and the mill gate's queue mustn't") {
    const Level level = Shipped("3-15");
    CHECK(level.look.height == 1200.0f);
    const int x1 = NodeOf(level, "x1"), x2 = NodeOf(level, "x2"), t = NodeOf(level, "t"), f = NodeOf(level, "f");
    auto run = [&](int bridge, int turnstile) {
      Board board(level);
      REQUIRE(board.Place(PieceOf(board, PieceType::Bridge), bridge));
      REQUIRE(board.Place(PieceOf(board, PieceType::Turnstile), turnstile));
      REQUIRE(board.Place(PieceOf(board, PieceType::TankTall), t));
      REQUIRE(board.Place(PieceOf(board, PieceType::Overflow), f));
      return RunBoard(board);
    };
    {
      // Nothing placed: blue and amber meet on the Old Bridge, on the first screen.
      Board board(level);
      const Run empty = RunBoard(board);
      CHECK(empty.outcome.failure == Failure::Mud);
      CHECK(empty.outcome.fail_node == x1);
      CHECK(empty.outcome.fail_tick == 12);
    }
    {
      // The bridge at the mill gate's crossing, the lock at the Old Bridge: the city's one Lean answer.
      const Run lean = run(x2, x1);
      CHECK(lean.outcome.solved);
      CHECK(lean.outcome.ticks == 192);
      CHECK(level.pars.pieces == 4);
    }
    {
      // The other way round, blue's queue at the shut gate backs into the locked crossing: THREADLOCKED.
      const Run swapped = run(x1, x2);
      CHECK(swapped.outcome.jammed);
      CHECK(swapped.outcome.fail_node == NodeOf(level, "g"));
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
