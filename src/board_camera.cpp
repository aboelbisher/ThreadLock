#include "board_camera.h"

#include <algorithm>
#include <cmath>

namespace threadlock {

void BoardCamera::SetView(float visible, float height) {
  visible_ = visible;
  height_ = height;
  max_ = std::max(0.0f, height - visible);
  y_ = Clamp(y_);
  ride_target_ = Clamp(ride_target_);
  focus_target_ = Clamp(focus_target_);
}

float BoardCamera::Clamp(float y) const { return std::clamp(y, 0.0f, max_); }

void BoardCamera::Snap(float y) {
  const float to = Clamp(y);
  y_ = to;
  velocity_ = 0.0f;
  intro_ = false;
}

void BoardCamera::BeginDrag() {
  SkipIntro();
  dragging_ = true;
  velocity_ = 0.0f;
  if (riding_ || focused_) hold_ = kHoldSeconds;
}

void BoardCamera::Drag(float dy) {
  y_ = Clamp(y_ + dy);
  if (riding_ || focused_) hold_ = kHoldSeconds;
}

void BoardCamera::EndDrag(float velocity) {
  dragging_ = false;
  velocity_ = std::fabs(velocity) < kFlingStop ? 0.0f : velocity;
}

void BoardCamera::StartRide() {
  riding_ = true;
  ride_target_ = 0.0f;
  focused_ = false;
  hold_ = 0.0f;
  velocity_ = 0.0f;
}

void BoardCamera::Follow(float front_y, float speed) {
  if (!riding_ || front_y < 0.0f) return;
  ride_speed_ = speed;
  ride_target_ = std::max(ride_target_, Clamp(front_y - kFrontAt * visible_));
}

void BoardCamera::Focus(float centre, bool snap) {
  focused_ = true;
  focus_target_ = Clamp(centre - 0.5f * visible_);
  velocity_ = 0.0f;
  hold_ = 0.0f;
  intro_ = false;
  if (snap) Snap(focus_target_);
}

void BoardCamera::Release() {
  riding_ = false;
  focused_ = false;
  hold_ = 0.0f;
}

void BoardCamera::Intro(float from, float to, float seconds) {
  intro_ = true;
  intro_from_ = Clamp(from);
  intro_to_ = Clamp(to);
  intro_seconds_ = std::max(seconds, 0.01f);
  intro_t_ = 0.0f;
  velocity_ = 0.0f;
  y_ = intro_from_;
}

void BoardCamera::SkipIntro() {
  if (!intro_) return;
  intro_ = false;
  y_ = intro_to_;
}

void BoardCamera::Ease(float target, float rate, float dt) {
  float step = (target - y_) * (1.0f - std::exp(-rate * dt));
  const float most = kMaxSpeed * dt;
  step = std::clamp(step, -most, most);
  if (std::fabs(target - y_) < 0.01f) step = target - y_;
  y_ = Clamp(y_ + step);
}

void BoardCamera::Update(float dt) {
  if (intro_) {
    intro_t_ += dt;
    const float t = std::min(intro_t_ / intro_seconds_, 1.0f);
    y_ = intro_from_ + (intro_to_ - intro_from_) * t * t * (3.0f - 2.0f * t);
    if (t >= 1.0f) intro_ = false;
  } else {
    Step(dt);
  }
  moved_ = y_ != last_y_;
  last_y_ = y_;
}

void BoardCamera::Step(float dt) {
  if (auto_speed_ != 0.0f) {
    y_ = Clamp(y_ + auto_speed_ * dt);
    auto_speed_ = 0.0f;
    if (riding_ || focused_) hold_ = kHoldSeconds;
  }
  if (!dragging_ && velocity_ != 0.0f) {
    const float y = Clamp(y_ + velocity_ * dt);
    velocity_ = y == y_ + velocity_ * dt ? velocity_ * std::exp(-kFlingDecay * dt) : 0.0f;  // an edge stops it
    if (std::fabs(velocity_) < kFlingStop) velocity_ = 0.0f;
    y_ = y;
    if (riding_ || focused_) hold_ = kHoldSeconds;
  }
  if (!dragging_ && velocity_ == 0.0f) {
    if (hold_ > 0.0f) {
      hold_ = std::max(0.0f, hold_ - dt);
    } else if (focused_) {
      Ease(focus_target_, kFocusRate, dt);
    } else if (riding_) {
      Ease(ride_target_, kRideRate * ride_speed_, dt);
    }
  }
}

float LiquidFront(const Level& level, const Board& board) {
  float front = -1.0f;
  for (const Drop& drop : board.Drops()) {
    if (!drop.alive || drop.loc.kind == Loc::None) continue;
    front = std::max(front, DropPoint(level, drop, drop.loc).y);
  }
  return front;
}

}  // namespace threadlock
