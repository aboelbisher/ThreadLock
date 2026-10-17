#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "board_art.h"
#include "liquid.h"
#include "palette.h"
#include "threadlock_rules.h"
#include "wlw/ui/canvas.h"
#include "wlw/ui/font.h"

// How a board looks and answers touches (DESIGN.md §8, §9): any level drawn from its data. The world's
// BoardArt draws the place, the pipes and the parts (the back on the bloomed Picture layer, the liquid
// over it, the parts' fronts on the Overlay); this view adds what is the same in every world: the
// pieces (one brass toolkit), sockets, flaps, hints and the markers of what went wrong.
//
// Board units (a 360 x 600 board, y down) map to canvas units through a Layout the screen sets.
namespace threadlock {

// How to draw a piece. `node` (when it sits on the board) turns it to fit its pipes.
struct PieceLook {
  int setting = 0;
  float alpha = 1.0f;
  float spin = 0.0f;            // turnstile arms / mixer swirl
  float arm = -1.0f;            // flipper: the side it points to now (0..1, animated); -1 = from the setting
  int remaining = -1;           // overflow: how many still go its way (-1 = n)
  bool locked = false;          // a padlock in its corner
  Rgba accent = Hex(0xF6DA9A);  // arrows: which lane goes first, an overflow's way
};
// A piece of the brass toolkit (the same in every world).
void DrawPiece(wlw::ui::Canvas& canvas, const Piece& piece, glm::vec2 center, float scale, const PieceLook& look,
               const Palette& palette, const Level* level = nullptr, const Node* node = nullptr,
               const wlw::ui::Font* font = nullptr);

class BoardView {
 public:
  struct Options {
    int drag_type = -1;    // a piece being dragged (PieceType): every free socket lights up
    int drag_target = -1;  // the socket it would land in
    int hint_node = -1;    // a hint points here
    bool ghosts = false;   // the solution's pieces drawn faintly (Show the solution)
    const Placement* ghost_placement = nullptr;
  };

  void SetLevel(const Level* level);
  const Level* GetLevel() const { return level_; }
  // The world's way of drawing (it outlives the view).
  void SetArt(BoardArt* art);
  BoardArt& Art() const { return *art_; }
  void SetLayout(const Layout& layout) { layout_ = layout; }
  const Layout& GetLayout() const { return layout_; }

  // Animations and markers follow the board's events.
  void OnEvents(const std::vector<Event>& events, const Board& board);
  void Update(float dt, const Board& board);
  void ClearMarkers();

  // ---- One frame: BeginFrame, then the layers in order ----
  void BeginFrame(const Board& board, const Palette& palette, const wlw::ui::Font& font, float time, float dt,
                  bool calm, bool editing);
  void DrawBack(wlw::ui::Canvas& picture);
  void DrawLiquid(Liquid& liquid, float alpha);  // alpha: how far between the last tick and the next
  void DrawParts(wlw::ui::Canvas& canvas, const Options& options);
  void DrawMarkers(wlw::ui::Canvas& canvas);
  void DrawSpotlight(wlw::ui::Canvas& canvas, int node, float amount) const;

  // Hit tests, in canvas units.
  int SocketNear(glm::vec2 point, float radius) const;                     // any piece goes in any socket
  int PieceNear(const Board& board, glm::vec2 point, float radius) const;  // a placed piece's tray index
  int FlapNear(const Board& board, glm::vec2 point, float radius) const;   // a tappable flap's fork
  glm::vec2 NodeCanvas(int node) const { return layout_.ToCanvas(level_->nodes[static_cast<size_t>(node)].at); }
  // Where a piece on `node` is drawn (a twin gate sits between its two halves).
  glm::vec2 PieceCenter(const Board& board, int node) const;

 private:
  struct Marker {
    enum Kind { Mud, NearMiss, WrongCup, Spill, Dry, Jam } kind;
    int node;
    float age;
    int slot = -1;  // a rack's bottle
  };

  // Where a fork's flap handle is drawn: just off its centre, or outside a piece that sits on it (a
  // tank or a turnstile leaves the flap in charge).
  glm::vec2 FlapHandle(const Board& board, int fork) const;
  // Where a marker or the spotlight for `node` (and a rack's bottle) goes, in canvas units.
  glm::vec2 MarkerCanvas(int node, int slot) const;

  const Level* level_ = nullptr;
  BoardArt* art_ = nullptr;
  Layout layout_;
  ArtFrame frame_;
  std::vector<Marker> markers_;
  std::vector<NodeAnim> anims_;
  std::vector<float> cup_fill_;                  // animated fill per node (drops)
  std::vector<std::vector<float>> bottle_fill_;  // the same per rack bottle
};

}  // namespace threadlock
