#pragma once

#include "threadlock_rules.h"

// The camera of a tall board (a grand machine, Level::look.height > one screen): which stretch of it the
// screen shows. One dimension, in board units: Y() = the board y at the top of the view (0 = the top,
// MaxY() = the bottom of the board at the bottom of the view). Plain C++ (no engine): tested on the PC.
//
// Who moves it, strongest first: an intro pan; the player's finger (drag, then a fling that decays); a
// focus (a failure, Show me, a hint, the payoff); the ride (while a run plays, the view follows the
// liquid's front down and never climbs). A drag during a ride holds the view for kHoldSeconds, then the
// ride takes over again.
namespace threadlock {

class BoardCamera {
 public:
  static constexpr float kHoldSeconds = 2.5f;  // after a finger moved the view during a ride
  static constexpr float kFrontAt = 0.62f;     // the ride keeps the liquid's front this far down the view
  static constexpr float kMaxSpeed = 1200.0f;  // units per second, any easing
  static constexpr float kFlingDecay = 4.0f;   // v *= exp(-decay dt)
  static constexpr float kFlingStop = 15.0f;   // units per second
  static constexpr float kFocusRate = 6.0f;    // easing rate (1 / s)
  static constexpr float kRideRate = 3.5f;     // x the run's speed

  // The view shows `visible` units of a board `height` tall. Keeps Y() in range.
  void SetView(float visible, float height);
  float Y() const { return y_; }
  float MaxY() const { return max_; }
  float Visible() const { return visible_; }
  float Height() const { return height_; }
  bool Scrolls() const { return max_ > 0.0f; }
  bool Moved() const { return moved_; }  // Y() changed since the Update before the last one

  void Snap(float y);  // jumps there and stops every motion (not the ride or focus)

  // The finger. `dy` = board units the view moves (down = positive); `velocity` the same per second.
  void BeginDrag();
  void Drag(float dy);
  void EndDrag(float velocity);
  bool Dragging() const { return dragging_; }
  // Edge auto-scroll while a piece is dragged near the view's top or bottom: units per second, for this
  // frame only (call every frame it applies).
  void AutoScroll(float speed) { auto_speed_ = speed; }

  // The run. StartRide eases the view to the top, then Follow moves it down with the front.
  void StartRide();
  void Follow(float front_y, float speed = 1.0f);  // front_y < 0: nothing to follow
  bool Riding() const { return riding_; }
  // Centres the view on board y `centre` (wins over the ride until StartRide or Release).
  void Focus(float centre, bool snap = false);
  bool Focused() const { return focused_; }
  void Release();  // no ride, no focus: the view stays where it is

  // A pan from `from` to `to` (board y of the view's top) over `seconds`, smoothstep. A touch skips it.
  void Intro(float from, float to, float seconds);
  void SkipIntro();
  bool InIntro() const { return intro_; }

  void Update(float dt);

 private:
  float Clamp(float y) const;
  void Ease(float target, float rate, float dt);
  void Step(float dt);  // Update without an intro

  float visible_ = 600.0f, height_ = 600.0f, max_ = 0.0f;
  float y_ = 0.0f;
  float last_y_ = 0.0f;
  bool moved_ = false;
  bool dragging_ = false;
  float velocity_ = 0.0f;
  float auto_speed_ = 0.0f;
  float hold_ = 0.0f;
  bool riding_ = false;
  float ride_target_ = 0.0f;
  float ride_speed_ = 1.0f;
  bool focused_ = false;
  float focus_target_ = 0.0f;
  bool intro_ = false;
  float intro_from_ = 0.0f, intro_to_ = 0.0f, intro_seconds_ = 1.0f, intro_t_ = 0.0f;
};

// The lowest point (largest board y) of any drop still on the board, -1 if none: what the ride follows.
float LiquidFront(const Level& level, const Board& board);

}  // namespace threadlock
