#pragma once

#include <array>
#include <memory>
#include <vector>

#include <glm/glm.hpp>

#include "threadlock_rules.h"
#include "wlw/app/application.h"
#include "wlw/app/system.h"
#include "wlw/fx/flow_particles.h"
#include "wlw/scene/orthographic_camera.h"
#include "wlw/scene/post_effects.h"
#include "wlw/scene/sprite_batch.h"
#include "wlw/scene/viewport.h"
#include "wlw/ui/font.h"

namespace threadlock {

// The game's one system: the board (rules) and how it looks. The rules tick at a fixed rate and
// report events; the liquid is FlowParticles dots drawn into an offscreen viewport (blur +
// threshold = liquid) that the screen shows under the board's pieces.
//
// Positions are "board units": a 360 x 760 portrait board, y down, placed in the middle of the
// UI canvas (TwoCups' layout below). The liquid's camera shows the same units (world = (x, -y)).
class ThreadlockApp : public wlw::app::System {
 public:
  static wlw::app::AppConfig MakeConfig();

  void OnStart() override;
  void OnFixedUpdate(float dt) override;
  void OnUpdate(float dt) override;
  void OnRenderUI(wlw::ui::UIContext& ui) override;

 private:
  enum class Phase { Edit, Run, Done };

  struct Drag {
    Piece piece = Piece::None;
    int from = -1;          // the socket it came from (-1 = the tray)
    glm::vec2 start{0.0f};  // board units
    glm::vec2 at{0.0f};
  };

  void ApplyQuality(int level);
  void ResetLiquid();  // empty pipes, the source's swirl refilled
  void StartRun();
  void StopRun();
  void HandleEvents(const std::vector<Event>& events);
  void Burst(int path, Liquid liquid);  // one blob's dots at the start of `path`
  void UpdateStops();
  void HandleInput(const wlw::ui::UIContext& ui);

  glm::vec2 ToCanvas(glm::vec2 board) const { return board + offset_; }
  glm::vec2 SocketPos(int node) const;
  glm::vec2 PiecePos(Piece piece) const;  // in a socket or the tray
  int SocketNear(glm::vec2 board) const;  // -1 = none close enough

  void DrawBackground(wlw::ui::Canvas& canvas);  // under the liquid (Picture layer)
  void DrawBoard(wlw::ui::Canvas& canvas);       // over it (Overlay)
  void DrawPiece(wlw::ui::Canvas& canvas, Piece piece, glm::vec2 board, float scale, float alpha, float spin);

  Board board_{TwoCups::Make()};
  Phase phase_ = Phase::Edit;
  float tick_clock_ = 0.0f;
  float time_ = 0.0f;
  float collide_flash_ = 0.0f;
  float spin_ = 0.0f, spin_target_ = 0.0f;  // the turnstile's arms
  Drag drag_;
  bool dragging_ = false;
  glm::vec2 offset_{0.0f};  // board units -> canvas units

  // Liquid
  std::unique_ptr<wlw::fx::FlowParticles> flow_;
  std::shared_ptr<wlw::scene::SpriteBatch> dots_;
  std::shared_ptr<wlw::scene::Viewport> liquid_;
  std::shared_ptr<wlw::scene::OrthographicCamera> camera_;
  wlw::scene::Bloom bloom_;
  wlw::ui::Canvas* picture_ = nullptr;
  std::vector<int> segment_path_;      // per rules segment: its FlowParticles path
  std::vector<bool> segment_stopped_;  // what UpdateStops last set
  std::array<int, 2> through_path_{};  // the crossing's lanes: blue lane, amber lane
  std::array<int, 2> cup_path_{};      // blue cup, amber cup
  std::array<int, 2> swirl_path_{};    // the source's two loops
  float density_ = 1.0f;

  std::shared_ptr<wlw::ui::Font> font_;
};

}  // namespace threadlock
