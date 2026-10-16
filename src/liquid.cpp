#include "liquid.h"

#include <algorithm>
#include <cmath>

#include "wlw/core/log.h"

namespace threadlock {
namespace {

constexpr size_t kCapacity = 16000;
constexpr int kBlobDots = 16;  // per drop at full density
constexpr float kDotSize = 10.0f;

// A cheap, stable hash: the same drop always has the same shape.
uint32_t Hash(uint32_t x) {
  x ^= x >> 16;
  x *= 0x7FEB352Du;
  x ^= x >> 15;
  x *= 0x846CA68Bu;
  x ^= x >> 16;
  return x;
}

float Unit(uint32_t h) {  // -1..1
  return static_cast<float>(h & 0xFFFF) / 32767.5f - 1.0f;
}

}  // namespace

void Liquid::Create(wlw::app::Application& app) {
  using namespace wlw;
  dots_.reserve(kCapacity);
  batch_ = std::make_shared<scene::SpriteBatch>(kCapacity);
  camera_ = scene::OrthographicCamera::Create();
  viewport_ = scene::Viewport::Create({.name = "Liquid", .format = rendering::TextureFormat::RGBA16F, .depth = false});
  viewport_->SetCamera(camera_);
  viewport_->GetScene()->AddNode(batch_);
  scene::AddBlur(*viewport_, {.scale = 1.0f, .spread = 1.0f, .format = rendering::TextureFormat::RGBA16F});
  scene::AddThreshold(*viewport_, {.threshold = 0.35f, .softness = 0.05f});
  app.Window().AddViewport(viewport_);

  scene::Viewport& screen = app.Window().GetScreen();
  screen.SetClearColor(ui_color::kBackground);
  picture_ = &screen.AddCanvasLayer({.mode = scene::CanvasMode::Picture});  // pipes + liquid: bloomed
  bloom_ = scene::AddBloom(screen, {.threshold = 0.6f, .strength = 0.6f});
}

void Liquid::ApplyQuality(wlw::app::Application& app, int level) {
  struct Tier {
    float density, liquid, bloom;
  };
  static constexpr Tier kTiers[] = {{0.45f, 0.35f, 0.125f}, {0.7f, 0.5f, 0.25f}, {1.0f, 0.5f, 0.25f}};
  const Tier& tier = kTiers[std::clamp(level, 0, 2)];
  density_ = tier.density;
  viewport_->SetSizeFromScreen(tier.liquid);
  bloom_.SetScale(app.Window().GetScreen(), tier.bloom);
  WLW_LOG_INFO("Quality level %d: liquid density %.2f, scale %.2f, bloom %.3f", level, tier.density, tier.liquid,
               tier.bloom);
}

void Liquid::SetActive(bool active) {
  if (active == active_) return;
  active_ = active;
  viewport_->SetUpdateMode(active ? wlw::scene::Viewport::UpdateMode::Always
                                  : wlw::scene::Viewport::UpdateMode::Disabled);
  if (!active) batch_->Clear();
}

void Liquid::Begin(glm::vec2 canvas_size, float time) {
  time_ = time;
  dots_.clear();
  // The camera sees canvas units (world = (x, -y)), so dots line up with what the canvas draws.
  camera_->SetVisibleArea(canvas_size.x, canvas_size.y);
  const glm::vec2 eye{canvas_size.x * 0.5f, -canvas_size.y * 0.5f};
  camera_->SetPosition({eye.x, eye.y, 10.0f});
  camera_->SetTarget({eye.x, eye.y, 0.0f});
}

void Liquid::Dot(glm::vec2 at, float size, uint32_t color) {
  if (dots_.size() >= kCapacity) return;
  dots_.push_back({{at.x, -at.y}, size, color});
}

void Liquid::Blob(glm::vec2 at, glm::vec2 along, float size, Rgba front, Rgba back, uint32_t seed) {
  const glm::vec2 across{-along.y, along.x};
  const int n = std::max(6, static_cast<int>(std::lround(static_cast<float>(kBlobDots) * density_)));
  const float dot = kDotSize * (size / 10.0f) / std::sqrt(std::sqrt(density_));
  const uint32_t front_color = wlw::rendering::PackColor(front), back_color = wlw::rendering::PackColor(back);
  for (int k = 0; k < n; ++k) {
    const uint32_t h = Hash(seed * 0x9E3779B9u + static_cast<uint32_t>(k) * 0x85EBCA6Bu);
    const bool is_front = k % 2 == 0;
    float u = std::fabs(Unit(h)) * 0.42f;
    if (!is_front) u = -u;
    const float v = Unit(Hash(h)) * 0.22f;
    const float phase = Unit(Hash(h + 7)) * 3.14159f;
    const float wobble = calm_ ? 0.0f : std::sin(time_ * 2.3f + phase) * 0.06f;
    const glm::vec2 p = at + along * (u * size) + across * ((v + wobble) * size);
    const float scale = 0.85f + 0.3f * static_cast<float>((h >> 20) & 0xFF) / 255.0f;
    Dot(p, dot * scale, is_front ? front_color : back_color);
  }
}

void Liquid::Pool(glm::vec2 min, glm::vec2 max, float level, Rgba color, uint32_t seed) {
  level = std::clamp(level, 0.0f, 1.2f);
  if (level <= 0.001f) return;
  const float spacing = 5.5f / std::sqrt(density_);
  const float top = max.y - (max.y - min.y) * level;
  const uint32_t packed = wlw::rendering::PackColor(color);
  int row = 0;
  for (float y = max.y - spacing * 0.6f; y > top; y -= spacing, ++row) {
    for (float x = min.x + spacing * 0.6f; x < max.x - spacing * 0.3f; x += spacing) {
      const uint32_t h = Hash(seed + static_cast<uint32_t>(row) * 977u + static_cast<uint32_t>(x * 13.0f));
      const float surface = !calm_ && y - top < spacing * 1.5f ? std::sin(time_ * 3.0f + x * 0.15f) * 1.2f : 0.0f;
      Dot({x + Unit(h) * spacing * 0.3f, y + Unit(Hash(h)) * spacing * 0.3f + surface}, spacing * 2.0f, packed);
    }
  }
}

void Liquid::End() {
  const std::span<wlw::rendering::SpriteInstance> out = batch_->Write(dots_.size());
  std::copy(dots_.begin(), dots_.begin() + static_cast<std::ptrdiff_t>(out.size()), out.begin());
  batch_->FitBounds();
}

void Liquid::Draw(wlw::ui::Canvas& canvas) const {
  if (!active_) return;
  if (const auto& texture = viewport_->GetTexture()) {
    wlw::ui::ImageStyle style;
    style.uv = {{0.0f, 1.0f}, {1.0f, 0.0f}};  // render targets are bottom-up
    canvas.Image({{0.0f, 0.0f}, canvas.Size()}, texture, style);
  }
}

}  // namespace threadlock
