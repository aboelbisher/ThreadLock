#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "threadlock_app.h"
#include "wlw/app/screen.h"

// The map (DESIGN.md §8): one long pipe network you scroll through, a node per level. Solving a
// level pours light into the pipe to the next one; worlds sit behind valves that open with stars.
// The daily puzzle and the Lab are cards above it.
namespace threadlock {

class MapScreen : public wlw::app::Screen {
 public:
  MapScreen(ThreadlockApp& app, const Level* focus);

  void OnEnter() override;
  void Update(float dt) override;
  void RenderUI(wlw::ui::UIContext& ui) override;
  bool OnBack() override { return false; }  // the app goes to the background

 private:
  struct Spot {
    const Level* level;
    glm::vec2 at;  // content units (scrolls)
    bool challenge;
  };
  struct Header {
    int world;
    float y;
  };

  void Build(float width);
  void DrawWorld(wlw::ui::UIContext& ui, wlw::ui::Canvas& picture, float scroll_top);

  ThreadlockApp& app_;
  const Level* focus_;
  std::vector<Spot> spots_;
  std::vector<Header> headers_;
  float content_height_ = 0.0f;
  float built_width_ = 0.0f;
  float scroll_ = 0.0f, velocity_ = 0.0f;
  bool focused_ = false;
  bool pressed_ = false, moved_ = false;
  glm::vec2 press_at_{0.0f};
  float press_scroll_ = 0.0f, last_y_ = 0.0f;
  float view_top_ = 0.0f, view_bottom_ = 0.0f;
};

}  // namespace threadlock
