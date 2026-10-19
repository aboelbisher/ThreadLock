#pragma once

#include <memory>
#include <vector>

#include "apothecary_look.h"
#include "art_shapes.h"
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
// What doesn't move (the planked wall, the shelves with the shop's own jars, the spouts' stoneware jars and
// their slates, the counter, the sink, the bottles' glass, the cork board and its slips) is drawn once into
// a still picture, a column of screen-sized tiles (tile_pool.h) redrawn only when the level or the screen
// changes, with the copper lines' dark bores in it. Over the liquid each frame: the copper, with a porthole
// per cell where the drops show and glint, brass fittings at the bends and a copper boss with a porthole on
// every junction; the bottles' tonic filling up behind their glass, the chalk tally of each jar's drops,
// the shop bell. A rack is a lazy susan on the counter: its turntable on the bloomed picture, its bottles
// (depth-sorted, each with a parchment tag on its neck), its tap, the drop falling from it and the clock
// counting to its next turn over the liquid. Mills and gates are still the shared machine parts.
namespace threadlock {

class ApothecaryArt : public BoardArt {
 public:
  void Create(wlw::app::Application& app);

  void Invalidate() override;
  void Release() override;
  void DrawBack(wlw::ui::Canvas& picture, const ArtFrame& frame) override;
  void DrawFront(wlw::ui::Canvas& canvas, const ArtFrame& frame) override;
  wlw::ui::Rect CupPool(const ArtFrame& frame, int cup) const override;
  // The jars are opaque stoneware, and the bottles' tonic is drawn over the liquid: neither has any.
  void SpoutLiquid(Liquid&, const ArtFrame&, int) const override {}
  void CupLiquid(Liquid&, const ArtFrame&, int) const override {}
  glm::vec2 CupTop(const ArtFrame& frame, int cup) const override;
  // A rack bottle's mouth, wherever the turntable has turned it.
  Vec2 BottleAt(const ArtFrame& frame, int rack, int slot) const override;
  void DrawMud(wlw::ui::Canvas& canvas, glm::vec2 at, float age, float scale, float time) const override;
  glm::vec2 PipePoint(const ArtFrame& frame, int pipe, float distance) const override;
  float DropSize() const override { return 8.0f; }
  float SocketRadius(const Node&) const override { return 15.0f; }
  Rgba Accent() const override { return Hex(0xFFB45C); }

  // The handwriting on the slips (Caveat Bold).
  const wlw::ui::Font& Hand() const { return *hand_; }

 private:
  struct TileArt {
    std::shared_ptr<wlw::rendering::ShaderMaterial> wall, vignette;
  };
  // A copper line in board units: its centre line with round bends, from node centre to node centre (the
  // drops follow it), and what's on it.
  struct Tube {
    art::Path whole;
    float lead = 0.0f;                // where pipe.points starts along whole
    float stretch = 1.0f;             // the rounded line's length / the rules' length
    glm::vec2 copper{0.0f};           // where the copper runs along whole (the parts at its ends cover the rest)
    std::vector<float> holes;         // per cell: its porthole along whole (< 0: none, under a fitting)
    std::vector<float> windows;       // where the copper is cut for a porthole (the cells' and a pass node's)
    std::vector<glm::vec2> bends;     // a brass fitting on each
    float top = 0.0f, bottom = 0.0f;  // its extent (for culling)
  };

  void Prepare(const ArtFrame& frame);
  void DrawCopper(wlw::ui::Canvas& canvas, const ArtFrame& frame, const Tube& tube, float top, float bottom) const;
  void DrawGlints(wlw::ui::Canvas& canvas, const ArtFrame& frame) const;
  void AddTile();
  // Into tile k's offscreen picture; `screen` = one screen, `total` = the whole still picture's height
  // (canvas units).
  void DrawStill(int slot, int k, const ArtFrame& frame, glm::vec2 screen, float total);
  void DrawShelf(wlw::ui::Canvas& canvas, const ArtFrame& frame, int shelf, float width) const;
  void DrawCounter(wlw::ui::Canvas& canvas, const ArtFrame& frame, float width, float bottom) const;
  void DrawCorkBoard(wlw::ui::Canvas& canvas, const ArtFrame& frame) const;
  // A spout's jar (on its shelf or its own bracket) and the slate beside it, without the chalk.
  void DrawJar(wlw::ui::Canvas& canvas, const ArtFrame& frame, int spout) const;
  // A drain: a copper sink set into the counter, or on a bracket of its own higher up the wall.
  void DrawSink(wlw::ui::Canvas& canvas, const ArtFrame& frame, int drain) const;
  // A cup's bottle as it stands: its shadow on the counter and the faint glass.
  void DrawBottleBack(wlw::ui::Canvas& canvas, const ArtFrame& frame, int cup) const;
  // The copper lines' dark bores and soft shadows, under the liquid.
  void DrawBores(wlw::ui::Canvas& canvas, const ArtFrame& frame) const;
  // Over the liquid: a bottle's tonic, glass, marks, nozzle and brass tag; a slate's chalk; the bell.
  void DrawBottle(wlw::ui::Canvas& canvas, const ArtFrame& frame, int cup) const;
  void DrawChalk(wlw::ui::Canvas& canvas, const ArtFrame& frame, int spout) const;
  void DrawBell(wlw::ui::Canvas& canvas, const ArtFrame& frame) const;
  // A rack: the turntable (on the picture, behind the liquid), the clock's face (still), and over the liquid
  // its bottles, their tags, the tap, the falling drop and the clock's hand.
  void DrawTurntable(wlw::ui::Canvas& picture, const ArtFrame& frame, int rack) const;
  void DrawClockFace(wlw::ui::Canvas& canvas, const ArtFrame& frame, int rack) const;
  void DrawRack(wlw::ui::Canvas& canvas, const ArtFrame& frame, int rack) const;

  TilePool tiles_;
  std::vector<TileArt> tile_art_;  // per slot
  std::shared_ptr<wlw::rendering::Shader> wall_shader_;
  std::shared_ptr<wlw::ui::Font> hand_;
  // This level's shop.
  const Level* level_ = nullptr;
  std::vector<Tube> tubes_;
  std::vector<apothecary::Jar> jars_;         // per node (a spout's)
  std::vector<int> cup_order_;                // per node: a cup's order (its slip's number - 1), -1 = none
  std::vector<std::vector<int>> rack_order_;  // per node, per bottle: a rack bottle's order, -1 = none
  std::vector<apothecary::Box> clear_;        // where the shop's own stock and herbs keep clear
  int slips_ = 0;
  float counter_ = 0.0f;  // the counter's top
  art::Glow glow_;
  WorkshopArt parts_;  // mud, until its own art (step 3c D)
};

}  // namespace threadlock
