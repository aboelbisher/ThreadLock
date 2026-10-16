#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "liquid.h"
#include "palette.h"
#include "threadlock_rules.h"
#include "wlw/ui/canvas.h"
#include "wlw/ui/font.h"

// How a board looks: any level drawn from its data (DESIGN.md §12 "view v2"). Pipes go on the
// bloomed Picture layer, the liquid comes from the rules' state each frame, and the pieces, cups,
// spouts and markers go on the Overlay over it.
//
// Board units (a 360 x 600 board, y down) map to canvas units through a Layout the screen sets.
namespace threadlock {

struct Layout {
  glm::vec2 origin{0.0f};
  float scale = 1.0f;
  glm::vec2 ToCanvas(Vec2 p) const { return origin + glm::vec2(p.x, p.y) * scale; }
  Vec2 ToBoard(glm::vec2 p) const { return {(p.x - origin.x) / scale, (p.y - origin.y) / scale}; }
};

inline constexpr glm::vec2 kBoardSize{360.0f, 600.0f};

// How to draw a piece. `node` (when it sits on the board) turns it to fit its pipes.
struct PieceLook {
  int setting = 0;
  float alpha = 1.0f;
  float spin = 0.0f;    // turnstile arms / mixer swirl
  float arm = -1.0f;    // flipper: the side it points to now (0..1, animated); -1 = from the setting
  int remaining = -1;   // overflow: how many still go its way (-1 = n)
  bool locked = false;  // a padlock in its corner
  float fill = 0.0f;    // tank: how full it is (0..1)
};
void DrawPiece(wlw::ui::Canvas& canvas, const Piece& piece, glm::vec2 center, float scale, const PieceLook& look,
               const Palette& palette, const Level* level = nullptr, const Node* node = nullptr,
               const wlw::ui::Font* font = nullptr);

class BoardView {
 public:
  struct Options {
    float time = 0.0f;
    int drag_type = -1;    // a piece being dragged (PieceType): the sockets it fits light up
    int drag_target = -1;  // the socket it would land in
    int hint_node = -1;    // a hint points here
    int spotlight = -1;    // Show me: everything else dims
    bool editing = true;
    bool ghosts = false;  // the solution's pieces drawn faintly (Show the solution)
    const Placement* ghost_placement = nullptr;
  };

  void SetLevel(const Level* level);
  const Level* GetLevel() const { return level_; }
  void SetLayout(const Layout& layout) { layout_ = layout; }
  const Layout& GetLayout() const { return layout_; }

  // Animations and markers follow the board's events.
  void OnEvents(const std::vector<Event>& events, const Board& board);
  void Update(float dt, const Board& board);
  void ClearMarkers();

  void DrawPipes(wlw::ui::Canvas& picture) const;
  void DrawLiquid(Liquid& liquid, const Board& board, float alpha, const Palette& palette, float time) const;
  void DrawParts(wlw::ui::Canvas& canvas, const Board& board, const Palette& palette, const wlw::ui::Font& font,
                 const Options& options) const;
  void DrawMarkers(wlw::ui::Canvas& canvas, const Board& board, const wlw::ui::Font& font, float time) const;
  void DrawSpotlight(wlw::ui::Canvas& canvas, int node, float amount) const;

  // Hit tests, in canvas units.
  int SocketNear(glm::vec2 point, float radius, int piece_type = -1) const;  // -1: any socket
  int PieceNear(const Board& board, glm::vec2 point, float radius) const;    // a placed piece's tray index
  int FlapNear(const Board& board, glm::vec2 point, float radius) const;     // a tappable flap's fork
  glm::vec2 NodeCanvas(int node) const { return layout_.ToCanvas(level_->nodes[static_cast<size_t>(node)].at); }
  // Where a piece on `node` is drawn (a twin gate sits between its two halves).
  glm::vec2 PieceCenter(const Board& board, int node) const;

 private:
  struct Marker {
    enum Kind { Mud, NearMiss, WrongCup, Spill, Dry, Jam } kind;
    int node;
    float age;
  };
  struct NodeAnim {
    float spin = 0.0f, spin_target = 0.0f;
    float arm = 0.0f;
    float flash = 0.0f;
  };

  const Level* level_ = nullptr;
  Layout layout_;
  std::vector<Marker> markers_;
  std::vector<NodeAnim> anims_;
  std::vector<float> cup_fill_;  // animated fill per node (drops)
};

}  // namespace threadlock
