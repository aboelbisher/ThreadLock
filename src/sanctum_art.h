#pragma once

#include <memory>
#include <vector>

#include "art_shapes.h"
#include "board_art.h"
#include "sigil_look.h"
#include "tile_pool.h"
#include "wlw/app/application.h"
#include "wlw/rendering/shader_material.h"
#include "wlw/ui/font.h"

// World 4, The Mixing Hall: the alchemists' sanctum (DESIGN.md §5), an obsidian table by candlelight seen from
// above. The machine is a sigil inlaid in gold (sigil_look.h): black gold-lipped grooves between glass globes on
// its points, element flasks at its rim pouring, phials under it filling with the elixir.
//
// What doesn't move (the table, its veins, the parchment and quill, the candles' wax, the sigil's rings, runes and
// figure, the grooves, the glass of the flasks, globes and phials, the seals' bases) is drawn once into a still
// picture, a column of screen-sized tiles (tile_pool.h). Over it each frame, on the bloomed picture: the candle
// flames and their pools of light, the stains of the colours that ran down each groove, the mixers' haze. The
// liquid is the shared one (metaballs) in the grooves; the flasks' and phials' contents, their pills, the mixers'
// swirl, the twin seals' bar and leaves, the payoff (the elixir rising, the sigil igniting back to the rim) and the
// failures (sludge, the lonely seal's crack, short, spill, wrong colour) are drawn on the overlay. Only sigil levels
// are drawn this way; World 4's older levels keep the workshop look.
namespace threadlock {

class SanctumArt : public BoardArt {
 public:
  // `sigil` = the sanctum's lettering (Cinzel Bold): the seals' and mixers' words.
  void Create(wlw::app::Application& app, std::shared_ptr<wlw::ui::Font> sigil);

  void Invalidate() override;
  void Release() override;
  void DrawBack(wlw::ui::Canvas& picture, const ArtFrame& frame) override;
  void DrawFront(wlw::ui::Canvas& canvas, const ArtFrame& frame) override;
  // The failures' moments are drawn in the sanctum (sludge, the jam, short, spill, wrong colour); a near miss
  // keeps the shared look.
  bool DrawMarker(wlw::ui::Canvas& canvas, const ArtFrame& frame, const Marker& marker) const override;
  float MomentLength(const Marker& marker) const override;
  wlw::ui::Rect CupPool(const ArtFrame& frame, int cup) const override;
  // The flasks' and phials' contents are drawn on the overlay (DrawFront), not as the liquid.
  void SpoutLiquid(Liquid&, const ArtFrame&, int) const override {}
  void CupLiquid(Liquid&, const ArtFrame&, int) const override {}
  glm::vec2 CupTop(const ArtFrame& frame, int cup) const override;
  void DrawMud(wlw::ui::Canvas& canvas, glm::vec2 at, float age, float scale, float time) const override;
  float DropSize() const override { return 10.0f; }
  float SocketRadius(const Node& node) const override;
  Rgba Accent() const override { return Hex(0xE6CC88); }

 private:
  struct TileArt {
    std::shared_ptr<wlw::rendering::ShaderMaterial> table, vignette;
  };
  // The colour that last ran down a groove, and since when (seconds): its stain fades in.
  struct Stain {
    Color color = Color::None;
    float since = 0.0f;
  };

  void Prepare(const ArtFrame& frame);
  void AddTile();
  void DrawStill(int slot, int k, const ArtFrame& frame, glm::vec2 screen, float total);
  void DrawTable(wlw::ui::Canvas& canvas, const ArtFrame& frame) const;
  void DrawInlay(wlw::ui::Canvas& canvas, const ArtFrame& frame, const Sigil& sigil, size_t index) const;
  void DrawGrooves(wlw::ui::Canvas& canvas, const ArtFrame& frame) const;
  void DrawGlass(wlw::ui::Canvas& canvas, const ArtFrame& frame) const;
  // Each frame, on the picture.
  void DrawCandles(wlw::ui::Canvas& picture, const ArtFrame& frame) const;
  void DrawStains(wlw::ui::Canvas& picture, const ArtFrame& frame);
  void DrawMixerBack(wlw::ui::Canvas& picture, const ArtFrame& frame, int node) const;
  // On the overlay.
  void DrawFlask(wlw::ui::Canvas& canvas, const ArtFrame& frame, int spout) const;
  void DrawPhial(wlw::ui::Canvas& canvas, const ArtFrame& frame, int cup) const;
  void DrawPill(wlw::ui::Canvas& canvas, const ArtFrame& frame, int cup) const;
  void DrawMixer(wlw::ui::Canvas& canvas, const ArtFrame& frame, int node) const;
  // A twin pair with a twin gate: the bar between its seals and the leaves opening as both halves let go.
  void DrawSeals(wlw::ui::Canvas& canvas, const ArtFrame& frame, int half, int twin);
  // A filled phial: its elixir rising and its grooves igniting back to the rim; once solved, the whole sigil.
  void DrawPayoff(wlw::ui::Canvas& canvas, const ArtFrame& frame, int cup) const;
  void DrawSolved(wlw::ui::Canvas& canvas, const ArtFrame& frame) const;
  // The moments.
  void DrawSludge(wlw::ui::Canvas& canvas, const ArtFrame& frame, const Marker& marker) const;
  void DrawJam(wlw::ui::Canvas& canvas, const ArtFrame& frame, const Marker& marker) const;
  void DrawCrack(wlw::ui::Canvas& canvas, const ArtFrame& frame, int node, float grown) const;
  void DrawPhialMoment(wlw::ui::Canvas& canvas, const ArtFrame& frame, const Marker& marker) const;
  // A line through these board points, in canvas units.
  std::vector<glm::vec2> OnCanvas(const ArtFrame& frame, const std::vector<Vec2>& points) const;
  std::vector<glm::vec2> OnCanvas(const ArtFrame& frame, const std::vector<glm::vec2>& points) const;
  bool Bad(const ArtFrame& frame, int cup) const;  // its pill goes red

  TilePool tiles_;
  std::vector<TileArt> tile_art_;  // per slot
  std::shared_ptr<wlw::rendering::Shader> table_shader_;
  std::shared_ptr<wlw::ui::Font> sigil_;
  art::Glow glow_;
  // This level's sanctum (board units).
  const Level* level_ = nullptr;
  std::vector<Sigil> sigils_;
  std::vector<std::vector<glm::vec2>> grooves_;          // per pipe: GroovePoints with round bends
  std::vector<sigil::Vessel> vessels_;                   // per node: a flask's or a phial's (r 0 = none)
  std::vector<float> globes_;                            // per node: its glass's radius (0 = none)
  std::vector<std::vector<sigil::Stroke2>> runes_;       // per sigil
  std::vector<std::vector<std::vector<Vec2>>> figures_;  // per sigil: FigureLines
  std::vector<Vec2> candles_;
  std::vector<std::vector<sigil::Ignite>> ignites_;  // per node: a cup's IgniteFrom
  std::vector<Stain> stains_;                        // per pipe
  std::vector<int> released_;                        // per node: the last tick both twin halves let go
};

}  // namespace threadlock
