#pragma once

#include <span>
#include <vector>

#include <glm/glm.hpp>

#include "barges.h"
#include "palette.h"
#include "threadlock_rules.h"
#include "wlw/ui/canvas.h"
#include "wlw/ui/font.h"

// A world's way of drawing a machine (DESIGN.md §5: each world builds and reads its boards in its own
// way). BoardView keeps the state (animations, fills, markers), the pieces and the input; a BoardArt
// draws the place, the pipes and the parts, and says where the liquid goes.
namespace threadlock {

class Liquid;

// Board units (a 360 x 600 board, y down) -> canvas units: set by the screen. A tall board
// (Level::look.height > 600) scrolls: `scroll` = the board y at the top of the view, 0..scroll_max (board units).
struct Layout {
  glm::vec2 origin{0.0f};
  float scale = 1.0f;
  float scroll = 0.0f;
  float scroll_max = 0.0f;
  glm::vec2 ToCanvas(Vec2 p) const { return origin + glm::vec2(p.x, p.y - scroll) * scale; }
  Vec2 ToBoard(glm::vec2 p) const { return {(p.x - origin.x) / scale, (p.y - origin.y) / scale + scroll}; }
  Layout Unscrolled() const { return {origin, scale, 0.0f, scroll_max}; }
  glm::vec2 ScrollOffset() const { return {0.0f, -scroll * scale}; }  // canvas units, unscrolled -> this
  bool operator==(const Layout&) const = default;
};

// One screen's worth of board: every level is this wide, and at least this tall.
inline constexpr glm::vec2 kBoardSize{360.0f, 600.0f};

// What the view animates per node from the board's events.
struct NodeAnim {
  float spin = 0.0f, spin_target = 0.0f;  // turnstile arms, mixer swirl, mill wheel, rack turntable
  float arm = 0.0f;                       // flipper
  float flash = 0.0f;                     // a mix, a gate flipping
  int tick = -1000000;                    // the tick of its last pour, leave or flip (long ago = never)
  int flip = -1000000;                    // the tick of a gate's last flip (its leaves swing the tick after)
  Color mixed = Color::None;              // the last mix's colour (World 4's mixer globe)
  int mix = -1000000;                     // and its tick
};

// A moment on the board, shown where it happened (from the board's events): what went wrong, a near miss,
// an order filled. The view keeps them and draws its own words for them; a world may draw them its way.
struct Marker {
  enum Kind { Mud, NearMiss, WrongCup, Spill, Dry, Jam, Paid } kind;
  int node;
  float age;      // seconds since it happened (real time, whatever the run's speed)
  int slot = -1;  // a rack's bottle
};

// One frame of a board, for the art to read.
struct ArtFrame {
  const Level* level = nullptr;
  const Board* board = nullptr;
  Layout layout;
  const Palette* palette = nullptr;
  const wlw::ui::Font* font = nullptr;
  float time = 0.0f;   // 0 with reduced motion
  float dt = 0.0f;     // seconds since the last frame
  float alpha = 1.0f;  // how far the drops are between the last tick and the next (set with the liquid)
  bool calm = false;   // reduced motion: nothing drifts or flickers
  bool editing = true;
  std::span<const NodeAnim> anims;
  std::span<const float> cup_fill;                       // per node: drops in it (animated)
  std::span<const std::vector<float>> bottle_fill;       // per rack, per bottle
  std::span<const std::vector<barges::Landing>> landed;  // per cup: the barges that landed, in order
  std::span<const Marker> markers;

  float Scale() const { return layout.scale; }
  glm::vec2 At(int node) const { return layout.ToCanvas(level->nodes[static_cast<size_t>(node)].at); }
  // The board clock in ticks, with the drops' place between two ticks (alpha).
  float Clock() const { return board ? static_cast<float>(board->Ticks()) + alpha : 0.0f; }
};

class BoardArt {
 public:
  virtual ~BoardArt() = default;

  // A board comes on screen: whatever the art keeps from earlier frames may be stale.
  virtual void Invalidate() {}
  // The board left the screen: free what only a big board needed.
  virtual void Release() {}

  // Behind the liquid, on the bloomed Picture layer: the place, the pipes, the backs of the parts.
  virtual void DrawBack(wlw::ui::Canvas& picture, const ArtFrame& frame) = 0;
  // Over the liquid, under the pieces: the parts' fronts and their labels.
  virtual void DrawFront(wlw::ui::Canvas& canvas, const ArtFrame& frame) = 0;
  // Whether the drops are the shared liquid (metaballs); a world that draws its drops itself answers false.
  virtual bool UsesLiquid() const { return true; }
  // The drops, drawn by the world (only when UsesLiquid is false), on the Picture layer after the back.
  virtual void DrawDrops(wlw::ui::Canvas&, const ArtFrame&) {}
  // The liquid: drops, cups' pools, spouts' swirls (canvas units).
  void DrawLiquid(Liquid& liquid, const ArtFrame& frame, float alpha) const;
  // A drop `distance` board units along pipe.points, in canvas units.
  virtual glm::vec2 PipePoint(const ArtFrame& frame, int pipe, float distance) const;
  // Where a cup's liquid lies: it fills this rectangle from the bottom.
  virtual wlw::ui::Rect CupPool(const ArtFrame& frame, int cup) const = 0;
  // A spout's swirl of what it still has to pour.
  virtual void SpoutLiquid(Liquid& liquid, const ArtFrame& frame, int spout) const = 0;
  // A cup's liquid: a pool filling CupPool from the bottom. A world that draws what's in its cups
  // itself (over the liquid) leaves it out.
  virtual void CupLiquid(Liquid& liquid, const ArtFrame& frame, int cup) const;
  // Where bottle `slot` of a rack is (board units): its markers ("dry", "spill") go over it.
  virtual Vec2 BottleAt(const ArtFrame& frame, int rack, int slot) const;
  // Just above a cup: where "dry", "spill" and "wrong colour" go.
  virtual glm::vec2 CupTop(const ArtFrame& frame, int cup) const = 0;
  // Mud in a junction, `age` seconds after it happened.
  virtual void DrawMud(wlw::ui::Canvas& canvas, glm::vec2 at, float age, float scale, float time) const = 0;
  // A moment drawn the world's way, over the pieces: true = drawn (the view leaves out its own words for it).
  // A world that shows a moment in its parts (DrawFront reads frame.markers) answers true and draws nothing.
  virtual bool DrawMarker(wlw::ui::Canvas&, const ArtFrame&, const Marker&) const { return false; }
  // How long a moment plays (seconds): the result card waits for it.
  virtual float MomentLength(const Marker&) const { return 0.0f; }
  // Where "Show me" shines its light for a failure at this node (and a rack's bottle), in canvas units.
  virtual glm::vec2 Spotlight(const ArtFrame& frame, int node, int slot) const;
  // A drop's size in the liquid (board units): it must stay inside the world's pipes.
  virtual float DropSize() const { return 12.0f; }
  // How far out a free socket's ring is drawn (it goes round the part that holds the socket).
  virtual float SocketRadius(const Node&) const { return 19.0f; }
  // Free sockets, hints and highlights.
  virtual Rgba Accent() const = 0;
};

}  // namespace threadlock
