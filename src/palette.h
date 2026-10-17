#pragma once

#include <array>
#include <string_view>

#include "threadlock_rules.h"
#include "wlw/core/color.h"

// Threadlock's colours: the liquids (a palette the player can change, DESIGN.md §7 Collections),
// and the glass-and-dark-metal look of the board and UI.
namespace threadlock {

using Rgba = wlw::core::Color;  // (Color is the rules' liquid enum)

constexpr Rgba Hex(unsigned rgb, float alpha = 1.0f) {
  return {static_cast<float>((rgb >> 16) & 0xFF) / 255.0f, static_cast<float>((rgb >> 8) & 0xFF) / 255.0f,
          static_cast<float>(rgb & 0xFF) / 255.0f, alpha};
}

inline Rgba WithAlpha(Rgba c, float a) { return {c.r, c.g, c.b, c.a * a}; }

inline Rgba Mixed(Rgba a, Rgba b, float t) {
  return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}

// The board and UI.
namespace ui_color {
inline constexpr Rgba kBackground = Hex(0x070B10);
inline constexpr Rgba kPipeEdge = Hex(0x13212E);
inline constexpr Rgba kPipeInside = Hex(0x0A121A);
inline constexpr Rgba kPanel = Hex(0x0D141C);
inline constexpr Rgba kPanelRaised = Hex(0x16212D);
inline constexpr Rgba kLine = Hex(0x1F2D3B);
inline constexpr Rgba kInk = Hex(0xD4E3F2);  // pieces, text
inline constexpr Rgba kMuted = Hex(0x8FA4B8);
inline constexpr Rgba kFaint = Hex(0x5E7286);
inline constexpr Rgba kGood = Hex(0x9EE3B0);  // solved, near misses
inline constexpr Rgba kBad = Hex(0xE09159);   // failures (mud text)
inline constexpr Rgba kStar = Hex(0xFFC24D);
}  // namespace ui_color

// A set of liquid colours. Every pair stays apart for colour-blind players (DESIGN.md §3 Colours);
// shapes on spouts, cups and separators back every colour up.
struct Palette {
  const char* key;
  const char* name;
  Rgba blue, amber, green, rose, pearl;
};

inline constexpr Rgba kMud = Hex(0x8C5C33);
inline constexpr Rgba kInkLiquid = Hex(0x1B1626);

inline constexpr std::array<Palette, 6> kPalettes = {{
    {"classic", "Classic", Hex(0x2F7BFF), Hex(0xFFC53D), Hex(0x3FE0A0), Hex(0xFF4FA3), Hex(0xF2F6FF)},
    {"contrast", "High contrast", Hex(0x0072B2), Hex(0xE69F00), Hex(0x009E73), Hex(0xCC79A7), Hex(0xF0F0F0)},
    {"neon", "Neon", Hex(0x00B3FF), Hex(0xFFE600), Hex(0x39FF88), Hex(0xFF2E9A), Hex(0xFFFFFF)},
    {"sunset", "Sunset", Hex(0x5B6CFF), Hex(0xFF9E3D), Hex(0x7FE07A), Hex(0xFF5E7E), Hex(0xFFF1E0)},
    {"deep_sea", "Deep Sea", Hex(0x1E90FF), Hex(0xF5D76E), Hex(0x2EE6C5), Hex(0xFF6FB5), Hex(0xE8FBFF)},
    {"prism", "Prism", Hex(0x4D7CFF), Hex(0xFFD23F), Hex(0x3CF0B0), Hex(0xF25CFF), Hex(0xFAFAFF)},
}};

inline const Palette& PaletteByKey(std::string_view key) {
  for (const Palette& palette : kPalettes) {
    if (key == palette.key) return palette;
  }
  return kPalettes[0];
}

inline Rgba LiquidColor(const Palette& palette, Color color) {
  switch (color) {
    case Color::Blue: return palette.blue;
    case Color::Amber: return palette.amber;
    case Color::Green: return palette.green;
    case Color::Rose: return palette.rose;
    case Color::Pearl: return palette.pearl;
    case Color::Ink: return kInkLiquid;
    case Color::Mud: return kMud;
    case Color::None: break;
  }
  return ui_color::kFaint;
}

}  // namespace threadlock
