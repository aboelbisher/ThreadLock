// The city map (city_map.h): the layout, the lantern path and its taps, the rose window's panes, what is
// lit and what's new since the last visit.
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>

#include "../src/city_map.h"
#include "../src/level_io.h"
#include "doctest.h"

#ifndef THREADLOCK_ASSETS
#define THREADLOCK_ASSETS "assets"
#endif

using namespace threadlock;
using namespace threadlock::city;

namespace {

float Distance(Vec2 a, Vec2 b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y)); }
float Radius(Vec2 at) { return Distance(at, kWindow); }

// Four worlds of 15 levels and 3 challenges, gates as shipped.
MapLevels Levels() {
  MapLevels levels;
  for (int w = 1; w <= 4; ++w) {
    WorldLevels world;
    world.world = w;
    world.gate = w <= 1 ? 0 : 20 * (w - 1);
    for (int i = 1; i <= 15; ++i) world.main.push_back(std::to_string(w) + "-" + std::to_string(i));
    for (int i = 1; i <= 3; ++i) world.challenges.push_back(std::to_string(w) + "-C" + std::to_string(i));
    levels.push_back(world);
  }
  return levels;
}

constexpr int kAll = kSolveStar | kLeanStar | kSwiftStar;

StarMap Solved(int world, int count, int bits = kSolveStar) {
  StarMap stars;
  for (int i = 1; i <= count; ++i) stars[std::to_string(world) + "-" + std::to_string(i)] = bits;
  return stars;
}

std::string ReadText(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  std::stringstream text;
  text << file.rdbuf();
  return text.str();
}

}  // namespace

TEST_SUITE("city map") {
  TEST_CASE("the lantern path winds down the tower under the window, thumb-sized") {
    const auto& lanterns = Lanterns();
    for (int i = 0; i < kLanterns; ++i) {
      const Lantern& l = lanterns[i];
      CAPTURE(i);
      CHECK(l.diamond == (i >= kPetals));
      CHECK(l.hit * 2.0f >= 56.0f);
      CHECK(l.at.x - l.ring >= 0.0f);
      CHECK(l.at.x + l.ring <= kWidth);
      CHECK(l.at.y > kWindow.y + kWindowRim);  // under the window
      CHECK(DistrictAt(l.at.y) == 1);
      CHECK(l.stars_y > l.at.y + l.radius);
      if (i > 0 && i < kPetals) CHECK(l.at.y > lanterns[i - 1].at.y);  // 1-1 at the top, 1-15 at the bottom
      for (int j = 0; j < i; ++j) CHECK(Distance(l.at, lanterns[j].at) > l.radius + lanterns[j].radius + 10.0f);
    }
    CHECK(lanterns[0].at.x == doctest::Approx(195.0f));
    CHECK(lanterns[0].at.y == doctest::Approx(714.0f));
    CHECK(lanterns[14].at.y == doctest::Approx(714.0f + 14 * 84.0f));
    // The challenges branch off after 1-15.
    for (int i = kPetals; i < kLanterns; ++i) CHECK(lanterns[i].at.y > lanterns[13].at.y);
  }

  TEST_CASE("a tap picks the lantern under it, nothing between them") {
    for (int i = 0; i < kLanterns; ++i) {
      const Lantern& l = Lanterns()[i];
      CHECK(HitLantern(l.at) == i);
      CHECK(HitLantern({l.at.x + l.hit * 0.6f, l.at.y}) == i);
    }
    CHECK(HitLantern({10.0f, 760.0f}) == -1);
    CHECK(HitLantern(kWindow) == -1);  // the window is a picture, not a button
    // A diamond answers on its own shape: inside its corner, not the corner of its square.
    const Lantern& c1 = Lanterns()[kPetals];
    CHECK(HitLantern({c1.at.x, c1.at.y - 38.0f}) == kPetals);
    CHECK(HitLantern({c1.at.x + 25.0f, c1.at.y - 25.0f}) != kPetals);
  }

  TEST_CASE("the trail joins the lanterns in order, the side branch from 1-15") {
    const auto& trail = Trail();
    REQUIRE(trail.size() == 17);
    for (size_t p = 0; p < trail.size(); ++p) {
      CAPTURE(p);
      const std::vector<Vec2> points = TrailPoints(static_cast<int>(p), 8);
      REQUIRE(points.size() == 9);
      CHECK(Distance(points.front(), Lanterns()[trail[p].from].at) < 0.01f);
      CHECK(Distance(points.back(), Lanterns()[trail[p].to].at) < 0.01f);
    }
    CHECK(trail[13].from == 13);
    CHECK(trail[13].to == 14);
    CHECK(trail[14].from == 14);
    CHECK(trail[14].to == kPetals);
    CHECK(trail[16].to == kLanterns - 1);
  }

  TEST_CASE("the window's panes: fifteen petals around the heart, inside the rim") {
    for (int p = 0; p < kPetals; ++p) {
      CAPTURE(p);
      const Band tip = PetalTip(p, 8);
      REQUIRE(tip.inner.size() == tip.outer.size());
      for (size_t k = 0; k < tip.inner.size(); ++k) {
        CHECK(Radius(tip.inner[k]) == doctest::Approx(kTipInner).epsilon(0.001));
        CHECK(Radius(tip.outer[k]) >= kTipShoulder - 0.01f);
        CHECK(Radius(tip.outer[k]) < kWindowInside);
      }
      const float th = PetalAngle(p);
      CHECK(TipOuter(p, th - kPetalHalf) == doctest::Approx(kTipShoulder).epsilon(0.002));
      CHECK(TipOuter(p, th) > 160.0f);  // pointed in the middle
      CHECK(TipOuter(p, th) > TipOuter(p, th + 5.0f));
      // The colour slivers share the big pane out edge to edge.
      const Band first = PetalSliver(p, 0, 3, 4), last = PetalSliver(p, 2, 3, 4);
      CHECK(Distance(first.inner.front(), tip.inner.front()) < 0.05f);
      CHECK(Distance(first.outer.front(), tip.outer.front()) < 0.05f);
      CHECK(Distance(last.outer.back(), tip.outer.back()) < 0.05f);
      CHECK(Distance(PetalSliver(p, 0, 3, 4).outer.back(), PetalSliver(p, 1, 3, 4).outer.front()) < 0.05f);
      // Lean before the middle, Swift after it, between the heart and the big pane.
      const Band lean = LeanPane(p, 3), swift = SwiftPane(p, 3);
      CHECK(Distance(lean.inner.back(), swift.inner.front()) < 0.01f);
      CHECK(Radius(lean.inner[1]) == doctest::Approx(kSmallInner));
      CHECK(Radius(swift.outer[1]) == doctest::Approx(kSmallOuter));
      CHECK(kSmallInner > kHeart);
    }
    // Petal 0 (1-1) at the top, then clockwise.
    CHECK(PetalTip(0, 2).outer[1].x == doctest::Approx(kWindow.x));
    CHECK(PetalTip(0, 2).outer[1].y < kWindow.y);
    CHECK(PetalTip(1, 2).outer[1].x > kWindow.x);
    // Neighbours don't overlap.
    CHECK(24.0f > 2.0f * kPetalHalf);
    for (int k = 0; k < kLobes; ++k) CHECK(Radius(LobeCentre(k)) + kLobeRadius < kHeart);
  }

  TEST_CASE("the districts stack top to bottom; each sleeping district's lamps and pipes stay in it") {
    float y = 0.0f;
    for (int w = 1; w <= kDistricts; ++w) {
      const District& d = Districts()[w - 1];
      CHECK(d.world == w);
      CHECK(d.top == y);
      CHECK(d.bottom > d.top);
      y = d.bottom;
      CHECK(DistrictAt(d.top) == w);
      CHECK(DistrictAt(d.bottom - 1.0f) == w);
      if (w == 1) continue;
      const auto lamps = DistrictLamps(w);
      for (const Vec2& lamp : lamps) {
        CHECK(lamp.y >= d.top);
        CHECK(lamp.y <= d.bottom);
        CHECK(lamp.x > 40.0f);
        CHECK(lamp.x < kWidth - 40.0f);
      }
      for (int j = 0; j < kDistrictLamps; ++j) {
        CHECK(HitLamp(w, lamps[j]) == j);
        for (int i = 0; i < j; ++i) CHECK(Distance(lamps[i], lamps[j]) > kLampHit);  // no tap lands between two
      }
      CHECK(HitLamp(w, {lamps[0].x, lamps[0].y - kLampHit - 1.0f}) == -1);
      CHECK(d.valve < lamps[0].y);
      CHECK(d.plate > d.valve);
      CHECK(d.plate < lamps[0].y);
      const Cubic gate = GatePipe(w);
      CHECK(gate.a.y == doctest::Approx(d.valve + 22.0f));
      CHECK(Distance(gate.d, {lamps[0].x, lamps[0].y - 10.0f}) < 0.01f);
      if (w >= 3) {
        const Cubic link = LinkPipe(w);
        CHECK(link.a.y > DistrictLamps(w - 1)[kDistrictLamps - 1].y);
        CHECK(link.d.y == doctest::Approx(d.valve - 22.0f));
      }
    }
    CHECK(y == kHeight);
    // The tower's pipe runs from under the lanterns to World 2's valve.
    CHECK(kTowerPipeTop.y > Lanterns()[kLanterns - 1].stars_y);
    CHECK(kTowerPipeBottom.y < Districts()[1].valve);
    CHECK(PipeFill(0, 20) == 0.0f);
    CHECK(PipeFill(10, 20) == doctest::Approx(0.5f));
    CHECK(PipeFill(33, 20) == 1.0f);
  }

  TEST_CASE("scroll targets: a lantern two thirds down, the window, a gate") {
    CHECK(WindowScroll(kMockView) == doctest::Approx(190.0f));
    CHECK(LanternScroll(0, kMockView) == doctest::Approx(154.0f));
    CHECK(LanternScroll(10, kMockView) - LanternScroll(9, kMockView) == doctest::Approx(84.0f));
    CHECK(WindowInView(WindowScroll(kMockView), kMockView));
    CHECK(WindowInView(LanternScroll(1, kMockView), kMockView));
    CHECK(!WindowInView(LanternScroll(8, kMockView), kMockView));
    // A taller phone shows more: the targets scale with the view.
    CHECK(LanternScroll(5, 1000.0f) < LanternScroll(5, kMockView));
    CHECK(ClampScroll(-50.0f, 900.0f) == 0.0f);
    CHECK(ClampScroll(9000.0f, 900.0f) == kHeight - 900.0f);
    CHECK(GateScroll(2, kMockView) == doctest::Approx(Districts()[1].valve - 430.0f));
  }

  TEST_CASE("lights: one lantern open at the start, the next opens with each solve") {
    const MapLevels levels = Levels();
    auto lights = LanternLights(levels, {});
    CHECK(!lights[0].locked);
    CHECK(lights[1].locked);
    CHECK(lights[kPetals].locked);
    CHECK(CurrentLantern(lights) == 0);

    StarMap stars = Solved(1, 3);
    stars["1-2"] = kAll;
    lights = LanternLights(levels, stars);
    CHECK(lights[1].bits == kAll);
    CHECK(!lights[3].locked);
    CHECK(lights[4].locked);
    CHECK(CurrentLantern(lights) == 3);
    stars["1-6"] = kSolveStar;  // solved out of order (another device): never drawn locked
    CHECK(!LanternLights(levels, stars)[5].locked);
    CHECK(LanternLights(levels, stars)[4].locked);
    stars.erase("1-6");

    // The challenges open with 1-15, then breathe once the main path is done.
    stars = Solved(1, 15);
    lights = LanternLights(levels, stars);
    for (int i = kPetals; i < kLanterns; ++i) CHECK(!lights[i].locked);
    CHECK(CurrentLantern(lights) == kPetals);
    stars["1-C1"] = kSolveStar;
    stars["1-C2"] = kSolveStar;
    stars["1-C3"] = kSolveStar;
    CHECK(CurrentLantern(LanternLights(levels, stars)) == -1);
  }

  TEST_CASE("gates open with the stars before them; a shut district is all dark") {
    const MapLevels levels = Levels();
    StarMap stars = Solved(1, 15);  // 15 stars
    CHECK(MainStars(levels, stars, 1) == 15);
    CHECK(!GateOpen(levels, stars, 2));
    CHECK(OpenGates(levels, stars) == 1);
    auto lamps = DistrictLights(levels, stars, 2);
    for (const Light& lamp : lamps) CHECK(lamp.locked);

    stars["1-C1"] = kAll;  // challenges don't count toward gates
    CHECK(!GateOpen(levels, stars, 2));
    for (int i = 1; i <= 5; ++i) stars["1-" + std::to_string(i)] = kAll;  // + 10
    CHECK(StarsBefore(levels, stars, 2) == 25);
    CHECK(GateOpen(levels, stars, 2));
    CHECK(OpenGates(levels, stars) == 2);
    lamps = DistrictLights(levels, stars, 2);
    CHECK(!lamps[0].locked);
    CHECK(lamps[1].locked);
    CHECK(lamps[15].locked);
    CHECK(Dawn(0) == 0.0f);
    CHECK(Dawn(90) == 1.0f);
  }

  TEST_CASE("the first visit after 1-3 sees nothing yet; a restored save sees everything") {
    const MapLevels levels = Levels();
    StarMap first = Solved(1, 3);
    first["1-1"] = kAll;
    const Seen fresh = SeedSeen(levels, first);
    CHECK(fresh.stars.empty());
    const News news = WhatsNew(levels, fresh, first);
    REQUIRE(news.reveals.size() == 3);
    CHECK(news.reveals[0].level == "1-1");
    CHECK(news.reveals[0].fresh == kAll);
    CHECK(news.reveals[2].index == 2);
    CHECK(news.gates.empty());

    StarMap restored = Solved(1, 12, kAll);
    const Seen seeded = SeedSeen(levels, restored);
    CHECK(seeded.stars.size() == 12);
    CHECK(seeded.gates == 2);
    CHECK(WhatsNew(levels, seeded, restored).Empty());
  }

  TEST_CASE("what's new: fresh stars in map order and gates that opened") {
    const MapLevels levels = Levels();
    StarMap stars = Solved(1, 6, kSolveStar | kLeanStar);
    stars["1-7"] = kSolveStar | kGoldFrame;  // the gold frame isn't a star of its own
    Seen seen = SeeAll(levels, stars);
    CHECK(seen.stars["1-7"] == kSolveStar);
    CHECK(WhatsNew(levels, seen, stars).Empty());

    stars["1-3"] |= kSwiftStar;  // a replay won a star
    stars["1-8"] = kSolveStar;   // the next level
    News news = WhatsNew(levels, seen, stars);
    REQUIRE(news.reveals.size() == 2);
    CHECK(news.reveals[0].level == "1-3");
    CHECK(news.reveals[0].fresh == kSwiftStar);
    CHECK(news.reveals[1].index == 7);
    CHECK(news.reveals[1].fresh == kSolveStar);
    CHECK(news.gates.empty());

    // Enough for the market square's gate (20).
    for (int i = 1; i <= 8; ++i) stars["1-" + std::to_string(i)] = kAll;
    news = WhatsNew(levels, seen, stars);
    REQUIRE(news.gates.size() == 1);
    CHECK(news.gates[0] == 2);
    seen = SeeAll(levels, stars);
    CHECK(seen.gates == 2);
    CHECK(WhatsNew(levels, seen, stars).Empty());

    // A district's lamps report as world 2, main path then challenges.
    stars["2-1"] = kSolveStar;
    stars["2-C2"] = kSolveStar;
    news = WhatsNew(levels, seen, stars);
    REQUIRE(news.reveals.size() == 2);
    CHECK(news.reveals[0].world == 2);
    CHECK(news.reveals[0].index == 0);
    CHECK(news.reveals[1].index == 16);
  }

  TEST_CASE("each lantern's colours are its level's cups, left to right") {
    World world;
    std::vector<std::string> errors;
    REQUIRE(ParseWorld(ReadText(std::string(THREADLOCK_ASSETS) + "/levels/world1.json"), world, errors));
    // As the approved mock (A = amber, B = blue).
    const char* expected[kLanterns] = {"A",  "BA", "AB", "AB",  "AB",   "AB",  "BA",  "BAAB", "BA",
                                       "BA", "AB", "BA", "ABA", "BABA", "BAB", "BAB", "BA",   "AAB"};
    REQUIRE(world.levels.size() == kLanterns);
    for (int i = 0; i < kLanterns; ++i) {
      CAPTURE(world.levels[i].id);
      std::string got;
      for (Color c : LanternColors(world.levels[i])) got += c == Color::Amber ? 'A' : c == Color::Blue ? 'B' : '?';
      CHECK(got == expected[i]);
    }
  }
}
