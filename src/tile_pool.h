#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "palette.h"
#include "wlw/scene/viewport.h"
#include "wlw/scene/window.h"
#include "wlw/ui/canvas.h"

// A tall still picture (a grand machine's wall, the city map) as a column of screen-sized tiles: tile k
// covers still y [k h, (k + 1) h), h = the screen's canvas height. Each tile is drawn once into an
// offscreen viewport and redrawn only when what it shows changes (its owner keeps keys for that).
//
// The viewports are pooled: a tile is ~10 MB at a 1080 x 2280 screen, so a few slots are reused as the
// view scrolls, the tiles on screen first, then the neighbour the view is heading for (Spare). A viewport
// added during a frame can only be drawn into from the next frame (the window sizes it when it first
// draws), so a new slot waits a frame (Slot::fresh, cleared by EndFrame).
namespace threadlock {

class TilePool {
 public:
  struct Slot {
    std::shared_ptr<wlw::scene::Viewport> view;
    int id = -1;        // Window::AddViewport
    int tile = -1;      // which tile of the column it holds (-1 = none)
    uint64_t base = 0;  // the owner's keys for what its picture shows (0 = no picture yet)
    uint64_t key = 0;
    bool fresh = true;  // added this frame: not sized yet
  };

  void Create(wlw::scene::Window& window, std::string name, Rgba clear);
  int Add();  // a new slot; returns its index
  void Shrink(int keep);
  int Count() const { return static_cast<int>(slots_.size()); }
  Slot& operator[](int slot) { return slots_[static_cast<size_t>(slot)]; }
  const Slot& operator[](int slot) const { return slots_[static_cast<size_t>(slot)]; }

  int Find(int tile) const;  // the slot holding it, -1 if none
  // The slot for tile k: the one holding it, else one not showing a tile in [first, last] (the one
  // furthest from them), its picture cleared. -1 if every slot is busy with those.
  int Hold(int tile, int first, int last);
  void Invalidate();  // every picture is stale (keys cleared)
  void EndFrame();

  // The tiles [first, last] that have a picture, drawn `scroll` canvas units down the column.
  void Draw(wlw::ui::Canvas& picture, int first, int last, float scroll) const;

  // Which tiles a view `scroll` down a column `total` tall, `h` a tile, shows.
  struct Span {
    int count = 1, first = 0, last = 0;
  };
  static Span Visible(float scroll, float h, float total);

 private:
  wlw::scene::Window* window_ = nullptr;
  std::string name_;
  Rgba clear_{};
  std::vector<Slot> slots_;
};

}  // namespace threadlock
