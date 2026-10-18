#include "tile_pool.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace threadlock {

void TilePool::Create(wlw::scene::Window& window, std::string name, Rgba clear) {
  window_ = &window;
  name_ = std::move(name);
  clear_ = clear;
}

int TilePool::Add() {
  using namespace wlw;
  Slot slot;
  slot.view = scene::Viewport::Create(
      {.name = name_, .format = rendering::TextureFormat::RGBA8, .depth = false, .clear = clear_});
  slot.view->SetScene(nullptr);
  slot.view->SetUpdateMode(scene::Viewport::UpdateMode::Disabled);
  slot.id = window_->AddViewport(slot.view);
  slots_.push_back(std::move(slot));
  return Count() - 1;
}

void TilePool::Shrink(int keep) {
  while (Count() > std::max(0, keep)) {
    window_->RemoveViewport(slots_.back().id);
    slots_.pop_back();
  }
}

int TilePool::Find(int tile) const {
  for (int i = 0; i < Count(); ++i) {
    if (slots_[static_cast<size_t>(i)].tile == tile) return i;
  }
  return -1;
}

int TilePool::Hold(int tile, int first, int last) {
  if (const int held = Find(tile); held >= 0) return held;
  int best = -1, best_distance = -1;
  for (int i = 0; i < Count(); ++i) {
    const Slot& slot = slots_[static_cast<size_t>(i)];
    if (slot.tile >= first && slot.tile <= last) continue;
    const int distance = slot.tile < 0 ? 1 << 20 : std::min(std::abs(slot.tile - first), std::abs(slot.tile - last));
    if (distance > best_distance) best = i, best_distance = distance;
  }
  if (best < 0) return -1;
  Slot& slot = slots_[static_cast<size_t>(best)];
  slot.tile = tile;
  slot.base = slot.key = 0;
  return best;
}

void TilePool::Invalidate() {
  for (Slot& slot : slots_) slot.base = slot.key = 0;
}

void TilePool::EndFrame() {
  for (Slot& slot : slots_) slot.fresh = false;
}

void TilePool::Draw(wlw::ui::Canvas& picture, int first, int last, float scroll) const {
  const glm::vec2 screen = picture.Size();
  for (int k = first; k <= last; ++k) {
    const int i = Find(k);
    if (i < 0 || slots_[static_cast<size_t>(i)].base == 0) continue;  // no picture yet
    if (const auto& texture = slots_[static_cast<size_t>(i)].view->GetTexture()) {
      wlw::ui::ImageStyle style;
      style.uv = {{0.0f, 1.0f}, {1.0f, 0.0f}};  // render targets are bottom-up
      const float top = static_cast<float>(k) * screen.y - scroll;
      picture.Image({{0.0f, top}, {screen.x, top + screen.y}}, texture, style);
    }
  }
}

TilePool::Span TilePool::Visible(float scroll, float h, float total) {
  Span span;
  span.count = std::max(1, static_cast<int>(std::ceil(total / h - 1e-4f)));
  span.first = std::clamp(static_cast<int>(std::floor(scroll / h)), 0, span.count - 1);
  span.last = std::clamp(static_cast<int>(std::floor((scroll + h - 0.01f) / h)), 0, span.count - 1);
  return span;
}

}  // namespace threadlock
