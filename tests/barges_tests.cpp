// World 3's barges (barges.h): on the solved 3-6, each drop stays in its canal's water, its heading is a unit
// vector, and the harbour takes its barges in landing order; the lock leaves and the glide ease in and out.
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "../src/barges.h"
#include "../src/level_io.h"
#include "../src/threadlock_rules.h"
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

// The shipped 3-6 with its pars, kept alive by the caller (a board refers to its level).
std::vector<World> LoadWorlds() {
  std::vector<World> worlds;
  World world;
  std::vector<std::string> errors;
  REQUIRE(ParseWorld(ReadText(std::string(THREADLOCK_ASSETS) + "/levels/world3.json"), world, errors));
  worlds.push_back(std::move(world));
  REQUIRE(ApplyPars(ReadText(std::string(THREADLOCK_ASSETS) + "/levels/pars.json"), worlds, errors));
  return worlds;
}

const Level& Level36(const std::vector<World>& worlds) {
  for (const Level& level : worlds[0].levels) {
    if (level.id == "3-6") return level;
  }
  FAIL("3-6 is not in world 3");
  return worlds[0].levels[0];
}

// How far a point is from a canal's middle line (the nearest of every canal's rounded way).
float DistanceToCanals(const std::vector<canal::Way>& ways, Vec2 p) {
  float best = 1e9f;
  for (const canal::Way& way : ways) {
    const std::vector<glm::vec2>& points = way.whole.points;
    for (size_t k = 0; k + 1 < points.size(); ++k) {
      const glm::vec2 a = points[k], b = points[k + 1];
      const glm::vec2 ab = b - a, ap = glm::vec2(p.x, p.y) - a;
      const float length2 = glm::dot(ab, ab);
      const float t = length2 > 0.0f ? std::clamp(glm::dot(ap, ab) / length2, 0.0f, 1.0f) : 0.0f;
      best = std::min(best, glm::length(ap - ab * t));
    }
  }
  return best;
}

}  // namespace

TEST_SUITE("barges") {
  TEST_CASE("the lock's leaves: open as it pours, shut again before the next pour") {
    CHECK(barges::LockOpen(-1.0f) == doctest::Approx(0.0f));
    CHECK(barges::LockOpen(0.0f) == doctest::Approx(0.0f));
    CHECK(barges::LockOpen(0.125f) == doctest::Approx(0.5f));
    CHECK(barges::LockOpen(0.25f) == doctest::Approx(1.0f));
    CHECK(barges::LockOpen(0.6f) == doctest::Approx(1.0f));
    CHECK(barges::LockOpen(0.75f) == doctest::Approx(1.0f));
    CHECK(barges::LockOpen(0.875f) == doctest::Approx(0.5f));
    CHECK(barges::LockOpen(1.0f) == doctest::Approx(0.0f));
    CHECK(barges::LockOpen(40.0f) == doctest::Approx(0.0f));
  }

  TEST_CASE("a landed barge glides into its berth over 1.5 ticks, easing out") {
    CHECK(barges::Glide(-2.0f) == doctest::Approx(0.0f));
    CHECK(barges::Glide(0.0f) == doctest::Approx(0.0f));
    CHECK(barges::Glide(0.75f) == doctest::Approx(0.75f));  // eased: 1 - (1 - 0.5)^2
    CHECK(barges::Glide(1.5f) == doctest::Approx(1.0f));
    CHECK(barges::Glide(9.0f) == doctest::Approx(1.0f));
    CHECK(barges::Glide(0.5f) > barges::Glide(0.25f) - 1e-6f);
  }

  TEST_CASE("a harbour's payoff: lamps come on, the warm glow swells and dies, the district's light runs out") {
    CHECK(barges::LampsAt(0.0f) == doctest::Approx(0.0f));
    CHECK(barges::LampsAt(0.4f) == doctest::Approx(0.5f));
    CHECK(barges::LampsAt(100.0f) == doctest::Approx(1.0f));
    CHECK(barges::WarmAt(0.0f) == doctest::Approx(0.0f));
    CHECK(barges::WarmAt(0.4f) == doctest::Approx(1.0f));
    CHECK(barges::WarmAt(barges::kWarmLength) == doctest::Approx(0.0f));
    CHECK(barges::WarmAt(100.0f) == doctest::Approx(0.0f));
    CHECK(barges::DistrictAt(-1.0f) == doctest::Approx(0.0f));
    CHECK(barges::DistrictAt(barges::kDistrictLength / 2.0f) == doctest::Approx(0.75f));  // eased out
    CHECK(barges::DistrictAt(barges::kDistrictLength) == doctest::Approx(1.0f));
    CHECK(barges::DistrictAt(100.0f) == doctest::Approx(1.0f));
  }

  TEST_CASE("the barges of 3-6 stay in their canals, head along them, and land in order at Lantern Row") {
    const std::vector<World> worlds = LoadWorlds();
    const Level& level = Level36(worlds);
    const std::vector<canal::Way> ways = canal::Ways(level);
    Board board(level);
    board.SetPlacement(level.pars.lean);
    board.Start();
    std::vector<std::vector<barges::Landing>> landed(level.nodes.size());
    int poses = 0;
    while (board.Running()) {
      for (const Event& e : board.Tick()) {
        if (e.type == EventType::Land) landed[static_cast<size_t>(e.node)].push_back({e.bead, board.Ticks()});
      }
      for (const float alpha : {0.0f, 0.5f, 1.0f}) {
        for (const Drop& drop : board.Drops()) {
          if (!drop.alive) continue;
          const barges::Pose pose = barges::PoseOf(level, ways, drop, alpha);
          ++poses;
          CHECK(std::fabs(glm::length(glm::vec2(pose.heading.x, pose.heading.y)) - 1.0f) < 1e-3f);
          CHECK(DistanceToCanals(ways, pose.at) <= 9.5f + 0.5f);  // the water is 19 wide
          CHECK(pose.fade >= 0.0f);
          CHECK(pose.fade <= 1.0f);
        }
      }
    }
    CHECK(poses > 0);
    CHECK(board.GetOutcome().solved);

    const int ca = NodeOf(level, "ca");  // Lantern Row
    REQUIRE(level.nodes[static_cast<size_t>(ca)].count > 0);
    const std::vector<barges::Landing>& row = landed[static_cast<size_t>(ca)];
    CHECK(static_cast<int>(row.size()) == level.nodes[static_cast<size_t>(ca)].count);
    for (size_t k = 0; k < row.size(); ++k) {
      CHECK(row[k].bead.a == Color::Amber);
      if (k > 0) CHECK(row[k].tick >= row[k - 1].tick);
    }
  }

  TEST_CASE("a gate's leaves swing over 0.6 tick once its flip lands, from the old state to the new") {
    CHECK(barges::GateOpenness(true, -1.0f) == doctest::Approx(0.0f));   // before the flip: the old (shut) state
    CHECK(barges::GateOpenness(false, -1.0f) == doctest::Approx(1.0f));  // the old state was open
    CHECK(barges::GateOpenness(true, 0.0f) == doctest::Approx(0.0f));
    CHECK(barges::GateOpenness(true, 0.3f) == doctest::Approx(0.75f));  // eased: 1 - (1 - 0.5)^2
    CHECK(barges::GateOpenness(true, 0.6f) == doctest::Approx(1.0f));
    CHECK(barges::GateOpenness(true, 9.0f) == doctest::Approx(1.0f));
    CHECK(barges::GateOpenness(false, 0.6f) == doctest::Approx(0.0f));
    CHECK(barges::GateOpenness(false, 9.0f) == doctest::Approx(0.0f));
  }

  TEST_CASE("3-6's gate flips at the end of the tick its mill's barge leaves") {
    const std::vector<World> worlds = LoadWorlds();
    const Level& level = Level36(worlds);
    const int mill = NodeOf(level, "m"), gate = NodeOf(level, "g");
    Board board(level);
    board.SetPlacement(level.pars.lean);
    board.Start();
    int leave_tick = -1, flip_tick = -1, turns_before = 0;
    while (board.Running() && flip_tick < 0) {
      for (const Event& e : board.Tick()) {
        if (e.type == EventType::Leave && e.node == mill && leave_tick < 0) {
          leave_tick = board.Ticks();
          turns_before = board.MillTurns(mill);
        }
        if (e.type == EventType::Flip && e.node == gate && flip_tick < 0) flip_tick = board.Ticks();
      }
    }
    REQUIRE(leave_tick >= 0);
    CHECK(flip_tick == leave_tick);  // the same tick: the pulse runs during it, the leaves swing after
    CHECK(turns_before == 1);
  }

  TEST_CASE("a just-poured drop slides out of its lock, fading in") {
    const std::vector<World> worlds = LoadWorlds();
    const Level& level = Level36(worlds);
    const std::vector<canal::Way> ways = canal::Ways(level);
    Board board(level);
    board.SetPlacement(level.pars.lean);
    board.Start();
    while (board.Running()) {
      board.Tick();
      for (const Drop& drop : board.Drops()) {
        if (!drop.alive || drop.loc.kind != Loc::OnPipe || drop.prev.kind != Loc::InNode) continue;
        if (level.nodes[static_cast<size_t>(drop.prev.index)].kind != NodeKind::Spout) continue;
        CHECK(barges::PoseOf(level, ways, drop, 0.0f).fade == doctest::Approx(0.0f));
        CHECK(barges::PoseOf(level, ways, drop, 1.0f).fade == doctest::Approx(1.0f));
        return;  // one is enough
      }
    }
    FAIL("no drop was poured in 3-6");
  }
}
