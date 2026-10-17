#pragma once

#include <span>
#include <vector>

#include <glm/glm.hpp>

#include "palette.h"
#include "threadlock_rules.h"
#include "wlw/ui/canvas.h"
#include "wlw/ui/font.h"

// A world's way of drawing a machine (DESIGN.md §5: each world builds and reads its boards in its own
// way). BoardView keeps the state (animations, fills, markers), the pieces and the input; a BoardArt
// draws the place, the pipes and the parts, and says where the liquid goes.
namespace threadlock {

class Liquid;

// Board units (a 360 x 600 board, y down) -> canvas units: set by the screen.
struct Layout {
  glm::vec2 origin{0.0f};
  float scale = 1.0f;
  glm::vec2 ToCanvas(Vec2 p) const { return origin + glm::vec2(p.x, p.y) * scale; }
  Vec2 ToBoard(glm::vec2 p) const { return {(p.x - origin.x) / scale, (p.y - origin.y) / scale}; }
  bool operator==(const Layout&) const = default;
};

inline constexpr glm::vec2 kBoardSize{360.0f, 600.0f};

// What the view animates per node from the board's events.
struct NodeAnim {
  float spin = 0.0f, spin_target = 0.0f;  // turnstile arms, mixer swirl, mill wheel, rack turntable
  float arm = 0.0f;                       // flipper
  float flash = 0.0f;                     // a mix, a gate flipping
};

// One frame of a board, for the art to read.
struct ArtFrame {
  const Level* level = nullptr;
  const Board* board = nullptr;
  Layout layout;
  const Palette* palette = nullptr;
  const wlw::ui::Font* font = nullptr;
  float time = 0.0f;  // 0 with reduced motion
  float dt = 0.0f;    // seconds since the last frame
  bool calm = false;  // reduced motion: nothing drifts or flickers
  bool editing = true;
  std::span<const NodeAnim> anims;
  std::span<const float> cup_fill;                  // per node: drops in it (animated)
  std::span<const std::vector<float>> bottle_fill;  // per rack, per bottle

  float Scale() const { return layout.scale; }
  glm::vec2 At(int node) const { return layout.ToCanvas(level->nodes[static_cast<size_t>(node)].at); }
};

class BoardArt {
 public:
  virtual ~BoardArt() = default;

  // A board comes on screen: whatever the art keeps from earlier frames may be stale.
  virtual void Invalidate() {}

  // Behind the liquid, on the bloomed Picture layer: the place, the pipes, the backs of the parts.
  virtual void DrawBack(wlw::ui::Canvas& picture, const ArtFrame& frame) = 0;
  // Over the liquid, under the pieces: the parts' fronts and their labels.
  virtual void DrawFront(wlw::ui::Canvas& canvas, const ArtFrame& frame) = 0;
  // The liquid: drops, cups' pools, spouts' swirls (canvas units).
  void DrawLiquid(Liquid& liquid, const ArtFrame& frame, float alpha) const;
  // A drop `distance` board units along pipe.points, in canvas units.
  virtual glm::vec2 PipePoint(const ArtFrame& frame, int pipe, float distance) const;
  // Where a cup's liquid lies: it fills this rectangle from the bottom.
  virtual wlw::ui::Rect CupPool(const ArtFrame& frame, int cup) const = 0;
  // A spout's swirl of what it still has to pour.
  virtual void SpoutLiquid(Liquid& liquid, const ArtFrame& frame, int spout) const = 0;
  // Just above a cup: where "dry", "spill" and "wrong colour" go.
  virtual glm::vec2 CupTop(const ArtFrame& frame, int cup) const = 0;
  // Mud in a junction, `age` seconds after it happened.
  virtual void DrawMud(wlw::ui::Canvas& canvas, glm::vec2 at, float age, float scale, float time) const = 0;
  // How far out a free socket's ring is drawn (it goes round the part that holds the socket).
  virtual float SocketRadius(const Node&) const { return 19.0f; }
  // Free sockets, hints and highlights.
  virtual Rgba Accent() const = 0;
};

}  // namespace threadlock
