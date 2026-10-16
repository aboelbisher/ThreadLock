#pragma once

#include <cmath>
#include <cstdio>
#include <string>

#include <glm/glm.hpp>

#include "palette.h"
#include "wlw/ui/canvas.h"
#include "wlw/ui/font.h"
#include "wlw/ui/theme.h"
#include "wlw/ui/ui_context.h"
#include "wlw/ui/widgets.h"

// Small pieces of UI the screens share: the theme (dark glass, the game's colours), time text,
// star rows.
namespace threadlock {

inline constexpr float kTickSeconds = 0.125f;  // 8 ticks a second: a sixteenth note at 120 BPM

inline void SetupTheme(wlw::ui::Theme& theme, const wlw::ui::Font* font) {
  theme.font = font;
  theme.primary = ui_color::kGood;
  theme.on_primary = Hex(0x08140C);
  theme.secondary = Hex(0x1C2A38);
  theme.on_secondary = ui_color::kInk;
  theme.danger = Hex(0xC4573A);
  theme.surface = Hex(0x0F1720, 0.98f);
  theme.surface_raised = Hex(0x172331);
  theme.track = Hex(0x0A1016);
  theme.on = ui_color::kGood;
  theme.text = ui_color::kInk;
  theme.text_muted = ui_color::kMuted;
  theme.ink = Hex(0x05080C);
  theme.scrim = Hex(0x03060A, 0.72f);
}

// Ticks at normal speed as "m:ss".
inline std::string TimeText(int ticks) {
  const int tenths = (ticks * 10 + 4) / 8;
  char text[16];
  std::snprintf(text, sizeof(text), "%d:%02d", tenths / 600, tenths / 10 % 60);
  return text;
}

inline wlw::ui::TextStyle Outlined(const Rgba& color, float alpha = 1.0f) {
  return wlw::ui::TextStyle::Outlined(color, ui_color::kBackground, alpha);
}

// One star: filled (earned now), dim (earned before), or an outline (not yet).
inline void StarShape(wlw::ui::Canvas& canvas, glm::vec2 c, float r, const Rgba& color) {
  constexpr float kStep = 3.14159265f / 5.0f;
  glm::vec2 points[10];
  for (int i = 0; i < 10; ++i) {
    const float a = -3.14159265f * 0.5f + kStep * static_cast<float>(i);
    const float radius = i % 2 == 0 ? r : r * 0.45f;
    points[i] = c + glm::vec2(std::cos(a), std::sin(a)) * radius;
  }
  for (int i = 0; i < 10; ++i) canvas.Triangle(c, points[i], points[(i + 1) % 10], color);
}

}  // namespace threadlock
