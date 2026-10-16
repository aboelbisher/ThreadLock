#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <glm/glm.hpp>

#include "palette.h"
#include "wlw/app/application.h"
#include "wlw/rendering/instance_buffer.h"
#include "wlw/scene/orthographic_camera.h"
#include "wlw/scene/post_effects.h"
#include "wlw/scene/sprite_batch.h"
#include "wlw/scene/viewport.h"
#include "wlw/ui/canvas.h"

// The glowing liquid. Each frame the board view describes where liquid is (a blob per drop, a pool
// per cup) in canvas units; Liquid turns that into thousands of soft dots in one SpriteBatch, drawn
// into an offscreen RGBA16F viewport whose blur + threshold passes melt the dots into liquid
// (metaballs). The screen shows that picture on a Picture canvas layer (over the pipes, under the
// pieces), and the screen's bloom makes it glow.
//
// Unlike the feel test, drops are drawn from the rules' own state every frame (no particle routing),
// so rewinding, speeding up and "Show me" are just different ticks of the same board.
namespace threadlock {

class Liquid {
 public:
  void Create(wlw::app::Application& app);
  void ApplyQuality(wlw::app::Application& app, int level);
  // Off while no board is on screen (the map): the viewport isn't drawn at all.
  void SetActive(bool active);
  bool Active() const { return active_; }

  // ---- One frame ----
  void Begin(glm::vec2 canvas_size, float time);
  // A drop: `at` its centre, `along` the unit direction it travels, `size` its length in canvas
  // units. Two colours = a blend (front half / back half).
  void Blob(glm::vec2 at, glm::vec2 along, float size, Rgba front, Rgba back, uint32_t seed);
  // A cup's contents: liquid filling `min..max` from the bottom up to `level` (0..1).
  void Pool(glm::vec2 min, glm::vec2 max, float level, Rgba color, uint32_t seed);
  void End();
  // The liquid picture, drawn on `canvas` (a Picture layer) over the whole screen.
  void Draw(wlw::ui::Canvas& canvas) const;

  wlw::ui::Canvas& Picture() { return *picture_; }
  void SetCalm(bool calm) { calm_ = calm; }  // reduced motion: no wobble
  float Density() const { return density_; }

 private:
  void Dot(glm::vec2 at, float size, uint32_t color);

  std::vector<wlw::rendering::SpriteInstance> dots_;
  std::shared_ptr<wlw::scene::SpriteBatch> batch_;
  std::shared_ptr<wlw::scene::Viewport> viewport_;
  std::shared_ptr<wlw::scene::OrthographicCamera> camera_;
  wlw::scene::Bloom bloom_;
  wlw::ui::Canvas* picture_ = nullptr;
  float density_ = 1.0f;
  float time_ = 0.0f;
  bool active_ = true;
  bool calm_ = false;
};

}  // namespace threadlock
