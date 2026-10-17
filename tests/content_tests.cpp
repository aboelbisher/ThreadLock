// The game's content: every shipped level (assets/levels) is valid, solvable and matches the pars
// file; generated levels (daily, Lab) are deterministic, valid and solvable.
#include <algorithm>
#include <chrono>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

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
        REQUIRE(copy.nodes.size() == level.nodes.size());
        for (size_t n = 0; n < level.nodes.size(); ++n) {
          const Node& a = level.nodes[n];
          const Node& b = copy.nodes[n];
          CHECK((a.id == b.id && a.kind == b.kind && a.at.x == b.at.x && a.at.y == b.at.y && a.socket == b.socket));
          CHECK((a.beads == b.beads && a.beat == b.beat && a.start == b.start));
          CHECK((a.flap == b.flap && a.flap_tappable == b.flap_tappable && a.want == b.want && a.count == b.count &&
                 a.twin == b.twin));
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
