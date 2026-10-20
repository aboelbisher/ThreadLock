#pragma once

#include <array>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "art_shapes.h"
#include "city_map.h"
#include "palette.h"
#include "tile_pool.h"
#include "wlw/app/application.h"
#include "wlw/rendering/shader_material.h"
#include "wlw/rendering/texture.h"
#include "wlw/ui/canvas.h"
#include "wlw/ui/font.h"

// The city map's look (DESIGN.md §5, the approved mock): the tower at dawn with its rose window and the
// lantern path, then the market square (World 2: the apothecary's shop window is its progress picture, lamps across
// the cobbles, a fountain), the canal quarter and the sanctum asleep in mist.
//
// What doesn't change while the map is open (the sky, the tower's stones, roofs, houses, canals, the
// sanctum, the dark glass of the window, the pipes' iron) is drawn into a pooled column of screen-sized
// tiles (tile_pool.h), each only when first seen or when the sky's dawn changes. Each frame then draws
// what is lit or moves, on the bloomed Picture layer (panes, lanterns, lamps, glows, light in the pipes,
// smoke and mist), and the numbers, stars and plates on the canvas above it.
namespace threadlock {

// One frame of the map, as the screen wants it drawn (the screen animates the numbers).
struct CityFrame {
  float scale = 1.0f;   // canvas units per map unit (the screen is kWidth map units wide)
  float scroll = 0.0f;  // the map y at the top of the screen
  glm::vec2 screen{0.0f};
  float time = 0.0f;  // 0 with reduced motion
  bool calm = false;
  const Palette* palette = nullptr;
  const wlw::ui::Font* font = nullptr;
  int stars = 0;      // as shown: the sky's dawn
  std::string tower;  // World 1's name, upper case, on the banner under the roof

  // World 1, per lantern (15, then the challenges): locked as drawn; how lit each of its three stars is
  // (solve, lean, swift; 0..1) and the flash as one lights; the same for the window's panes (a petal's
  // big pane, Lean and Swift; a challenge lights its lobe of the heart with its first).
  std::array<bool, city::kLanterns> locked{};
  std::array<std::array<float, 3>, city::kLanterns> lantern{}, lantern_flash{};
  std::array<std::array<float, 3>, city::kLanterns> pane{}, pane_flash{};
  std::array<std::vector<Color>, city::kLanterns> colors;  // each level's cups
  int breathe = -1;                                        // the lantern that breathes (the next to play)
  bool pulse = false;                                      // faster: just after an animation pointed at it
  float tower_fill = 0.0f;                                 // the tower's pipe, filling with stars toward World 2's gate

  struct District {
    std::string name;  // the world's: its plate reads "2 · Fair Shares"
    int gate = 0, have = 0;
    bool open = false;
    std::array<city::Light, city::kDistrictLamps> lamps{};
    int current = -1;
    float valve = 0.0f;  // the valve's wheel turning open, 0..1
    float pour = 0.0f;   // light down the gate's pipe, 0..1
    float wake = 0.0f;   // its first lamp waking, 0..1
  };
  std::array<District, 3> districts;  // worlds 2-4

  // World 2's market square as drawn (its lamps like World 1's lanterns: alphas 0..1 as each lights).
  struct Market {
    std::array<bool, city::kDistrictLamps> locked{};
    std::array<std::array<float, 3>, city::kDistrictLamps> lamp{}, lamp_flash{};  // its stars: solve, lean, swift
    std::array<std::vector<Color>, city::kDistrictLamps> colors;                   // each level's cups
    // The shop window: per level its bottle filling, corked, labelled, sealed in gold (a challenge: its
    // globe lights with the first), and a flash as one shows.
    std::array<std::array<float, 4>, city::kDistrictLamps> bottle{};
    std::array<float, city::kDistrictLamps> bottle_flash{};
    int breathe = -1;  // the lamp that breathes
    bool pulse = false;
    float bell = -1.0f;   // seconds since the door bell rang (< 0: it hangs still)
    float awake = 0.0f;   // the shop awake (its gate open): the sign lit, the mist lifted
  } market;
  const wlw::ui::Font* hand = nullptr;  // the bottles' labels (Caveat)

  glm::vec2 ToCanvas(Vec2 p) const { return {p.x * scale, (p.y - scroll) * scale}; }
  float ViewHeight() const { return screen.y / scale; }  // map units
};

class CityArt {
 public:
  void Create(wlw::app::Application& app);
  void Release();  // the tiles' memory, when the map closes
  // `heading` = which way the view moves (> 0 down): the spare tile is made ahead of it.
  void Draw(wlw::ui::Canvas& picture, wlw::ui::Canvas& canvas, const CityFrame& frame, float heading);

 private:
  // Map units -> some canvas: a tile's (scroll = its top) or the screen's.
  struct Xf {
    float s = 1.0f, scroll = 0.0f;
    glm::vec2 operator()(Vec2 p) const { return {p.x * s, (p.y - scroll) * s}; }
    glm::vec2 operator()(float x, float y) const { return {x * s, (y - scroll) * s}; }
  };
  struct Shape {  // a convex polygon, map units
    std::vector<Vec2> points;
    Rgba fill, stroke;
    float width = 1.0f;
    float top = 0.0f, bottom = 0.0f;
  };

  void Build();
  void DrawStill(int slot, int k, const CityFrame& frame, float tile_height);
  void DrawSky(wlw::ui::Canvas& c, const Xf& x, const CityFrame& frame);
  void DrawTower(wlw::ui::Canvas& c, const Xf& x, float top, float bottom);
  void DrawTown(wlw::ui::Canvas& c, const Xf& x, float top, float bottom);
  void DrawMarketStill(wlw::ui::Canvas& c, const Xf& x, float top, float bottom);

  void DrawLights(wlw::ui::Canvas& p, const Xf& x, const CityFrame& frame);
  void DrawWindow(wlw::ui::Canvas& p, const Xf& x, const CityFrame& frame);
  void DrawLanterns(wlw::ui::Canvas& p, wlw::ui::Canvas& c, const Xf& x, const CityFrame& frame);
  void DrawDistricts(wlw::ui::Canvas& p, wlw::ui::Canvas& c, const Xf& x, const CityFrame& frame);
  void DrawMist(wlw::ui::Canvas& p, const Xf& x, const CityFrame& frame);
  // World 2's district: the shop window, its globes and bell, the light on the square, the fountain, the lamps.
  void DrawMarket(wlw::ui::Canvas& p, wlw::ui::Canvas& c, const Xf& x, const CityFrame& frame);
  void DrawBottle(wlw::ui::Canvas& p, wlw::ui::Canvas& c, const Xf& x, const CityFrame& frame, int level);
  void DrawMarketLamps(wlw::ui::Canvas& p, wlw::ui::Canvas& c, const Xf& x, const CityFrame& frame);

  TilePool tiles_;
  std::shared_ptr<wlw::rendering::ShaderMaterial> sky_top_, sky_low_;  // ui_gradient: the sky's two halves
  std::shared_ptr<wlw::rendering::WTexture2D> soft_;                   // a radial falloff (mist, glows)
  art::Glow glow_;
  bool built_ = false;
  std::vector<Shape> shapes_;  // houses, roofs, bridges' decks...
  struct Dot {
    Vec2 at;
    float r, alpha;
  };
  std::vector<Dot> sky_stars_;
  std::array<std::vector<Vec2>, city::kDistricts> lamp_paths_;
  std::vector<std::vector<Vec2>> trail_;
};

}  // namespace threadlock
