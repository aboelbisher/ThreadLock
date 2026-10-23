#pragma once

#include "art_shapes.h"
#include "board_art.h"

// The workshop look (v2): dark pipes with faint cell ticks, plain cups and spouts. Worlds whose own
// art isn't built yet use it, and so does the Lab (DESIGN.md §11).
namespace threadlock {

class WorkshopArt : public BoardArt {
 public:
  void DrawBack(wlw::ui::Canvas& picture, const ArtFrame& frame) override;
  void DrawFront(wlw::ui::Canvas& canvas, const ArtFrame& frame) override;
  wlw::ui::Rect CupPool(const ArtFrame& frame, int cup) const override;
  void SpoutLiquid(Liquid& liquid, const ArtFrame& frame, int spout) const override;
  glm::vec2 CupTop(const ArtFrame& frame, int cup) const override;
  void DrawMud(wlw::ui::Canvas& canvas, glm::vec2 at, float age, float scale, float time) const override;
  Rgba Accent() const override { return ui_color::kGood; }
};

// The machine parts, the same in every look until a world draws its own: a turning rack (World 2),
// a mill and its gates (World 3).
void DrawMachinePart(wlw::ui::Canvas& canvas, const ArtFrame& frame, int node);
// Where bottle `slot` of a rack is drawn (board units): a turntable seen from above, the bottle under
// the rack's pipe at the top; it turns one bottle per step of the rack's animation.
Vec2 BottleAt(const ArtFrame& frame, int rack, int slot);
// The colour of a drop still to pour (a blend: both halves).
void DrawBead(wlw::ui::Canvas& canvas, const Bead& bead, glm::vec2 centre, float r, const Palette& palette,
              float alpha);

}  // namespace threadlock
