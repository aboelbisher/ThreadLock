#pragma once

#include <memory>
#include <vector>

#include "art_shapes.h"
#include "barges.h"
#include "board_art.h"
#include "canal_look.h"
#include "tile_pool.h"
#include "wlw/app/application.h"
#include "wlw/rendering/shader_material.h"
#include "wlw/ui/font.h"
#include "workshop_art.h"

// World 3, Rush Hour: The Canal City (DESIGN.md §5) at night, seen from above. The pipes are canals with round
// bends between the rooftops; a spout is a lock where barges of light wait moored, a cup is a harbour serving
// its district, a mill's rod runs along the streets to the gates it works.
//
// What doesn't move (the night ground, the rooftops and the park the city generates in the gaps, the canals'
// banks, quays and water, the quay lamps, the locks' chambers, the junctions' basins, the harbours' basins,
// piers and dark lamps) is drawn once into a still picture, a column of screen-sized tiles (tile_pool.h)
// redrawn only when the level or the screen changes. Over it each frame, on the bloomed picture: the current's
// dashes drifting down the canals. Only canal maps (a harbour names its district: canal::IsCanalMap) are drawn
// this way; World 3's older levels keep the workshop look.
//
// The barges (barges.h) are drawn here, not as the shared liquid: each drop is a barge of light on its canal, a
// harbour's landed barges sit in their berths, and a lock's leaves swing as it pours. The mill (its house, its
// wheel turning a notch a barge, its rods with a pulse along them to the gates) and the gates (their leaves
// swinging when flipped, the lamp) are drawn here too, so this look doesn't use the shared machine parts.
namespace threadlock {

class CanalArt : public BoardArt {
 public:
  // `street` = the city's lettering (Cormorant SC Bold): districts, street names.
  void Create(wlw::app::Application& app, std::shared_ptr<wlw::ui::Font> street);

  void Invalidate() override;
  void Release() override;
  void DrawBack(wlw::ui::Canvas& picture, const ArtFrame& frame) override;
  bool UsesLiquid() const override { return false; }
  void DrawDrops(wlw::ui::Canvas& picture, const ArtFrame& frame) override;
  void DrawFront(wlw::ui::Canvas& canvas, const ArtFrame& frame) override;
  // A harbour's berths.
  wlw::ui::Rect CupPool(const ArtFrame& frame, int cup) const override;
  // A lock's barges wait moored in its chamber (DrawBack): no swirl.
  void SpoutLiquid(Liquid&, const ArtFrame&, int) const override {}
  glm::vec2 CupTop(const ArtFrame& frame, int cup) const override;
  void DrawMud(wlw::ui::Canvas& canvas, glm::vec2 at, float age, float scale, float time) const override;
  // Along the canals' round bends.
  glm::vec2 PipePoint(const ArtFrame& frame, int pipe, float distance) const override;
  float SocketRadius(const Node& node) const override;
  Rgba Accent() const override { return Hex(0x7FE3D8); }

  const wlw::ui::Font& Street() const { return *street_; }

 private:
  struct TileArt {
    std::shared_ptr<wlw::rendering::ShaderMaterial> ground, vignette;
  };
  // How a barge is drawn: its alpha, and whether it's lit (glow, tail light, wake when moving). A moored or
  // berthed barge is dim or unlit.
  struct BargeLook {
    float alpha = 1.0f;
    bool lit = true;
  };

  void Prepare(const ArtFrame& frame);
  void AddTile();
  // Into tile k's offscreen picture; `screen` = one screen, `total` = the whole still picture's height
  // (canvas units).
  void DrawStill(int slot, int k, const ArtFrame& frame, glm::vec2 screen, float total);
  void DrawCity(wlw::ui::Canvas& canvas, const ArtFrame& frame, float top, float bottom) const;
  void DrawCanals(wlw::ui::Canvas& canvas, const ArtFrame& frame) const;
  void DrawLock(wlw::ui::Canvas& canvas, const ArtFrame& frame, int spout) const;
  void DrawHarbour(wlw::ui::Canvas& canvas, const ArtFrame& frame, int cup) const;
  // The current: faint dashes drifting along the canals (on the picture, each frame).
  void DrawFlow(wlw::ui::Canvas& picture, const ArtFrame& frame) const;
  // A lock's barges still to leave, moored in its chamber (two abreast, the next out nearest its gates).
  void DrawMoored(wlw::ui::Canvas& canvas, const ArtFrame& frame, int spout) const;
  // One barge on its canal (`pose` in board units), its hull in the bead's colours.
  void DrawBarge(wlw::ui::Canvas& canvas, const ArtFrame& frame, const barges::Pose& pose, const Bead& bead,
                 BargeLook look) const;
  // A harbour's landed barges, gliding from the basin's mouth into their berths (or rafting at the mouth).
  void DrawBerths(wlw::ui::Canvas& canvas, const ArtFrame& frame, int cup) const;
  // A harbour's pill: its glyph and district's name, and its count of barges.
  void DrawPill(wlw::ui::Canvas& canvas, const ArtFrame& frame, int cup) const;
  // A mill's house, wheel, axle and hub, and its rods (with their names: `ordinal` = how many mills came before it).
  void DrawMill(wlw::ui::Canvas& canvas, const ArtFrame& frame, int mill, int ordinal) const;
  // A mill's notch lights, its wheel's slats turning with the barges through it, its window, the pulse on its rods.
  void DrawMillFront(wlw::ui::Canvas& canvas, const ArtFrame& frame, int mill) const;
  // A gate's hinges, post and lamp housing.
  void DrawGate(wlw::ui::Canvas& canvas, const ArtFrame& frame, int gate) const;
  // A gate's leaves swinging as it flips, and its lamp (green open, red shut).
  void DrawGateFront(wlw::ui::Canvas& canvas, const ArtFrame& frame, int gate) const;

  TilePool tiles_;
  std::vector<TileArt> tile_art_;  // per slot
  std::shared_ptr<wlw::rendering::Shader> ground_shader_;
  std::shared_ptr<wlw::ui::Font> street_;
  WorkshopArt parts_;  // mud until this look draws its own
  // This level's city.
  const Level* level_ = nullptr;
  canal::City city_;
  std::vector<art::Path> canals_;         // canal::CityCanals with round bends (board units)
  std::vector<canal::Way> ways_;          // per pipe
  std::vector<canal::Lock> locks_;        // per node (a spout's)
  std::vector<canal::Harbour> harbours_;  // per node (a cup's)
  std::vector<canal::Mill> mills_;        // per node (a mill's)
  std::vector<canal::Gate> gates_;        // per node (a gate's)
  art::Glow glow_;
};

}  // namespace threadlock
