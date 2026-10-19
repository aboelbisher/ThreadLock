#pragma once

#include <memory>
#include <vector>

#include "apothecary_look.h"
#include "board_art.h"
#include "tile_pool.h"
#include "wlw/app/application.h"
#include "wlw/rendering/shader_material.h"
#include "wlw/ui/font.h"
#include "workshop_art.h"

// World 2, Fair Shares: The Apothecary (DESIGN.md §5), a shop on market day. Copper lines run along the
// shelves of the back wall and straight down to the counter, where the bottles stand; the orders they fill
// hang as handwritten slips on a cork board. A filled bottle is corked, labelled and paid.
//
// What doesn't move (the planked wall, the shelves with the shop's own jars, the counter, the cork board
// and its slips) is drawn once into a still picture, a column of screen-sized tiles (tile_pool.h) redrawn
// only when the level or the screen changes. The machine itself is the workshop look's for now (the copper
// lines, the jar, the bottles and the rack come in v3 step 3c).
namespace threadlock {

class ApothecaryArt : public BoardArt {
 public:
  void Create(wlw::app::Application& app);

  void Invalidate() override;
  void Release() override;
  void DrawBack(wlw::ui::Canvas& picture, const ArtFrame& frame) override;
  void DrawFront(wlw::ui::Canvas& canvas, const ArtFrame& frame) override;
  wlw::ui::Rect CupPool(const ArtFrame& frame, int cup) const override;
  void SpoutLiquid(Liquid& liquid, const ArtFrame& frame, int spout) const override;
  glm::vec2 CupTop(const ArtFrame& frame, int cup) const override;
  void DrawMud(wlw::ui::Canvas& canvas, glm::vec2 at, float age, float scale, float time) const override;
  Rgba Accent() const override { return Hex(0xFFB45C); }

  // The handwriting on the slips (Caveat Bold).
  const wlw::ui::Font& Hand() const { return *hand_; }

 private:
  struct TileArt {
    std::shared_ptr<wlw::rendering::ShaderMaterial> wall, vignette;
  };

  void AddTile();
  // Into tile k's offscreen picture; `screen` = one screen, `total` = the whole still picture's height
  // (canvas units).
  void DrawStill(int slot, int k, const ArtFrame& frame, glm::vec2 screen, float total);
  void DrawShelf(wlw::ui::Canvas& canvas, const ArtFrame& frame, int shelf, float width) const;
  void DrawCounter(wlw::ui::Canvas& canvas, const ArtFrame& frame, float width, float bottom) const;
  void DrawCorkBoard(wlw::ui::Canvas& canvas, const ArtFrame& frame) const;

  TilePool tiles_;
  std::vector<TileArt> tile_art_;  // per slot
  std::shared_ptr<wlw::rendering::Shader> wall_shader_;
  std::shared_ptr<wlw::ui::Font> hand_;
  WorkshopArt parts_;  // the machine, until its own art (step 3c)
};

}  // namespace threadlock
