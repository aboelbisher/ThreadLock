#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "art_shapes.h"
#include "board_art.h"
#include "rose_window.h"
#include "tile_pool.h"
#include "wlw/app/application.h"
#include "wlw/rendering/shader_material.h"
#include "wlw/rendering/texture.h"
#include "wlw/scene/viewport.h"

// World 1, The Lantern Works (DESIGN.md §5): the glassworks at the top of the tower, at dawn. A
// cascade seen from the side, in front of the great rose window: light pours from hanging crucibles
// through clear glass tubes with brass collars, crosses inside glass globes, and drips into lanterns
// that glow as they fill. Every lantern that fills lights panes of the window; a solved machine lights
// its heart. Mud chokes the light into grey smoke.
//
// What doesn't move (the wall, the window, the glass) is drawn once into a texture of its own (an
// offscreen viewport that updates only when the level, the screen or the window's light changes);
// each frame draws that picture, the glows, the liquid and the parts' brass fronts. A tall board's
// still picture is a column of screen-sized tiles (tile_pool.h: the whole machine, as if the screen were
// taller); the screen draws the ones it shows, offset by the scroll.
namespace threadlock {

class LanternArt : public BoardArt {
 public:
  void Create(wlw::app::Application& app);

  void Invalidate() override;
  void Release() override;
  void DrawBack(wlw::ui::Canvas& picture, const ArtFrame& frame) override;
  void DrawFront(wlw::ui::Canvas& canvas, const ArtFrame& frame) override;
  glm::vec2 PipePoint(const ArtFrame& frame, int pipe, float distance) const override;
  wlw::ui::Rect CupPool(const ArtFrame& frame, int cup) const override;
  void SpoutLiquid(Liquid& liquid, const ArtFrame& frame, int spout) const override;
  glm::vec2 CupTop(const ArtFrame& frame, int cup) const override;
  void DrawMud(wlw::ui::Canvas& canvas, glm::vec2 at, float age, float scale, float time) const override;
  float SocketRadius(const Node& node) const override;
  Rgba Accent() const override { return Hex(0xF6DA9A); }

  static constexpr int kPanes = kOuterPanes + kInnerPanes;

 private:
  // A pipe as drawn: its centre line with round bends (the drops follow it), stretched on to the
  // centres of the nodes it joins, and the runs of glass along it (not under globes, not where drops
  // fall through the air, not where it's a coil, which is drawn turn by turn).
  struct Tube {
    art::Path centre;              // pipe.points, rounded: the drops' path
    art::Path whole;               // centre, from node centre to node centre
    float lead = 0.0f;             // where centre starts along whole
    float stretch = 1.0f;          // centre's length / the rules' length
    std::vector<glm::vec2> glass;  // runs of glass: (from, to) along whole
    std::vector<glm::vec2> coil;   // a coil's helix (canvas units), empty if none
    glm::vec2 coil_at{0.0f};       // the coil's run along whole
    glm::vec2 drip{0.0f};          // the drip fall along whole (y <= x: none)
    bool collar_start = false, collar_end = false;
    // Through a pipe socket (one way in, one out) the glass runs on unbroken: the joint round the socket
    // belongs to the tube coming in; the tubes it joins stop short of it.
    art::Path joint;
    bool joined_start = false, joined_end = false;
  };

  void DrawGlass(wlw::ui::Canvas& canvas, const art::Path& path, float radius, bool in_front, float scale,
                 bool shine_start = true, bool shine_end = true) const;
  // What a slot of the tile pool needs to draw its piece of the still picture (still coordinates =
  // canvas coordinates at scroll 0). The pool's keys: base = what it shows apart from the window's
  // light, key = and with it.
  struct TileArt {
    std::shared_ptr<wlw::rendering::ShaderMaterial> wall;
    Layout layout;  // board -> this tile's canvas
    std::vector<Tube> tubes;
  };

  void DrawCoil(wlw::ui::Canvas& canvas, const Tube& tube, float scale) const;
  std::vector<Tube> MakeTubes(const Level& level, const Layout& layout) const;
  void Prepare(const ArtFrame& frame);
  void AddTile();
  void UpdatePanes(const ArtFrame& frame);
  // Into tile k's offscreen picture; `screen` = one screen, `total` = the whole still picture's height
  // (canvas units).
  void DrawStill(int slot, int k, const ArtFrame& frame, glm::vec2 screen, float total);
  void DrawTube(wlw::ui::Canvas& canvas, const ArtFrame& frame, const Tube& tube, int pipe) const;
  void DrawCrucible(wlw::ui::Canvas& canvas, const ArtFrame& frame, int node, bool front) const;
  void DrawLantern(wlw::ui::Canvas& canvas, const ArtFrame& frame, int node, bool front) const;
  void DrawGlobe(wlw::ui::Canvas& canvas, const ArtFrame& frame, int node, bool front) const;
  float WindowRadius(const ArtFrame& frame) const;  // 0: no window (the Lab)

  // The still picture.
  TilePool tiles_;
  std::vector<TileArt> tile_art_;  // per slot
  std::shared_ptr<wlw::rendering::Shader> wall_shader_;
  std::shared_ptr<wlw::rendering::WTexture2D> pane_texture_;

  // This level's tubes, for its layout at scroll 0.
  const Level* level_ = nullptr;
  Layout layout_;
  std::vector<Tube> tubes_;

  // The window's light: how lit each pane is (animated) and its colour.
  std::array<float, kPanes> pane_lit_{};
  std::array<Rgba, kPanes> pane_color_{};
  std::array<uint8_t, kPanes * 4> pane_pixels_{};

  art::Glow glow_;
};

}  // namespace threadlock
