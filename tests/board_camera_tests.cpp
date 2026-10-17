// The camera of a tall board (board_camera.h): scrolling by finger, the ride down with the liquid, focus.
#include <cmath>

#include "../src/board_camera.h"
#include "doctest.h"
#include "test_levels.h"

using namespace threadlock;

namespace {

constexpr float kDt = 1.0f / 60.0f;

void Play(BoardCamera& camera, float seconds) {
  for (float t = 0.0f; t < seconds; t += kDt) camera.Update(kDt);
}

BoardCamera Tall() {  // a 1200-unit board, 600 visible
  BoardCamera camera;
  camera.SetView(600.0f, 1200.0f);
  return camera;
}

}  // namespace

TEST_SUITE("board camera") {
  TEST_CASE("a board no taller than the view never moves") {
    BoardCamera camera;
    camera.SetView(640.0f, 600.0f);
    CHECK(!camera.Scrolls());
    camera.BeginDrag();
    camera.Drag(300.0f);
    camera.EndDrag(2000.0f);
    camera.AutoScroll(700.0f);
    camera.Focus(900.0f);
    Play(camera, 2.0f);
    CHECK(camera.Y() == 0.0f);
  }

  TEST_CASE("a drag moves the view and stops at the board's ends") {
    BoardCamera camera = Tall();
    CHECK(camera.MaxY() == 600.0f);
    camera.BeginDrag();
    camera.Drag(250.0f);
    CHECK(camera.Y() == 250.0f);
    camera.Drag(1000.0f);
    CHECK(camera.Y() == 600.0f);
    camera.Drag(-5000.0f);
    CHECK(camera.Y() == 0.0f);
    camera.EndDrag(0.0f);
    Play(camera, 1.0f);
    CHECK(camera.Y() == 0.0f);
  }

  TEST_CASE("a fling glides, slows down and stops at an end") {
    BoardCamera camera = Tall();
    camera.BeginDrag();
    camera.EndDrag(400.0f);
    camera.Update(kDt);
    CHECK(camera.Moved());
    Play(camera, 3.0f);
    // 400 / 4 = 100 units in all (exp decay), less the stop threshold's tail.
    CHECK(camera.Y() > 90.0f);
    CHECK(camera.Y() < 101.0f);
    const float y = camera.Y();
    Play(camera, 1.0f);
    CHECK(camera.Y() == y);
    CHECK(!camera.Moved());
    camera.BeginDrag();
    camera.EndDrag(50000.0f);  // far past the end
    Play(camera, 3.0f);
    CHECK(camera.Y() == 600.0f);
  }

  TEST_CASE("the ride goes to the top, then down with the front, and never climbs") {
    BoardCamera camera = Tall();
    camera.Snap(400.0f);
    camera.StartRide();
    Play(camera, 4.0f);
    CHECK(camera.Y() == 0.0f);
    float last = camera.Y();
    bool climbed = false;
    for (int frame = 0; frame < 600; ++frame) {
      // The front goes down to 1100, then back up to 300 (drops landed): the view must not follow it up.
      const float front = frame < 300 ? 200.0f + 3.0f * static_cast<float>(frame) : 1100.0f - 3.0f * (frame - 300.0f);
      camera.Follow(front);
      camera.Update(kDt);
      climbed |= camera.Y() < last - 1e-4f;
      last = camera.Y();
    }
    CHECK(!climbed);
    CHECK(camera.Y() == doctest::Approx(600.0f).epsilon(0.01));  // clamped at the bottom
  }

  TEST_CASE("the ride keeps the front at 62% of the view") {
    BoardCamera camera = Tall();
    camera.StartRide();
    camera.Follow(700.0f);
    Play(camera, 4.0f);
    CHECK(camera.Y() == doctest::Approx(700.0f - 0.62f * 600.0f).epsilon(0.001));
    camera.Follow(-1.0f);  // nothing left on the board: stays
    Play(camera, 1.0f);
    CHECK(camera.Y() == doctest::Approx(700.0f - 0.62f * 600.0f).epsilon(0.001));
  }

  TEST_CASE("focus centres a point, snaps if asked and wins over the ride") {
    BoardCamera camera = Tall();
    camera.Focus(900.0f, true);
    CHECK(camera.Y() == 600.0f);  // 900 - 300 = 600, the bottom
    camera.StartRide();
    CHECK(!camera.Focused());
    camera.Follow(300.0f);  // front near the top: the ride wants 0
    camera.Focus(700.0f);   // a failure lower down: eases there
    Play(camera, 3.0f);
    CHECK(camera.Y() == doctest::Approx(400.0f).epsilon(0.001));
    camera.Release();
    Play(camera, 1.0f);
    CHECK(camera.Y() == doctest::Approx(400.0f).epsilon(0.001));  // released: stays
  }

  TEST_CASE("a drag during a ride holds the view, then the ride takes over again") {
    BoardCamera camera = Tall();
    camera.StartRide();
    camera.Follow(900.0f);  // the ride wants 528
    Play(camera, 4.0f);
    CHECK(camera.Y() == doctest::Approx(528.0f).epsilon(0.001));
    camera.BeginDrag();
    camera.Drag(-400.0f);
    camera.EndDrag(0.0f);
    Play(camera, 2.0f);
    CHECK(camera.Y() == doctest::Approx(128.0f));  // still held
    Play(camera, 3.0f);
    CHECK(camera.Y() == doctest::Approx(528.0f).epsilon(0.001));  // back on the ride
  }

  TEST_CASE("auto-scroll moves by speed x time, both ways, for one frame") {
    BoardCamera camera = Tall();
    camera.AutoScroll(600.0f);
    camera.Update(0.5f);
    CHECK(camera.Y() == doctest::Approx(300.0f));
    camera.Update(0.5f);  // not asked again: stays
    CHECK(camera.Y() == doctest::Approx(300.0f));
    camera.AutoScroll(-200.0f);
    camera.Update(0.5f);
    CHECK(camera.Y() == doctest::Approx(200.0f));
  }

  TEST_CASE("the intro pans with a smoothstep and a touch skips it") {
    BoardCamera camera = Tall();
    camera.Intro(600.0f, 0.0f, 2.0f);
    CHECK(camera.Y() == 600.0f);
    Play(camera, 1.0f);
    CHECK(camera.Y() == doctest::Approx(300.0f).epsilon(0.05));
    Play(camera, 1.1f);
    CHECK(!camera.InIntro());
    CHECK(camera.Y() == 0.0f);
    camera.Intro(600.0f, 0.0f, 2.0f);
    camera.BeginDrag();
    CHECK(!camera.InIntro());
    CHECK(camera.Y() == 0.0f);
  }

  TEST_CASE("the liquid's front: the lowest drop on the board") {
    const Level level = MakeLevel(kTwoCups);
    Board board(level);
    board.Start();
    CHECK(LiquidFront(level, board) < 0.0f);  // nothing poured yet
    float last = -1.0f;
    for (int tick = 0; tick < 30; ++tick) {
      board.Tick();
      const float front = LiquidFront(level, board);
      CHECK(front >= last);  // the first drop leads the way down
      last = front;
    }
    CHECK(last > 140.0f);  // past the fork
    CHECK(last < 500.0f);
  }
}
