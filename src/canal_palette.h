#pragma once

#include "palette.h"

// World 3's canal city at night, the colours its boards (canal_art) and the map's canal quarter (city_art) share.
namespace threadlock::canal::color {

// Slate rooftops, a dark park, skylights and quay lamps.
inline constexpr Rgba kRoofTones[] = {Hex(0x161E33), Hex(0x1A233C), Hex(0x1D2742), Hex(0x141B2E), Hex(0x1F2A47)};
inline constexpr Rgba kRoofShade = Hex(0x000000, 0.22f), kRidge = Hex(0x2C3858), kChimney = Hex(0x0D1322);
inline constexpr Rgba kSkylight = Hex(0xFFD58A, 0.5f);
inline constexpr Rgba kTreeDark = Hex(0x10291C), kTreeLight = Hex(0x1A3D29);
inline constexpr Rgba kLampLight = Hex(0xFFE2A8), kLampGlow = Hex(0xFFC870, 0.18f);
// The canals: a stone bank, a quay inside it, the water; the current's faint dashes.
inline constexpr Rgba kBank = Hex(0x2A3550), kQuay = Hex(0x3B4A6C), kWater = Hex(0x0B1F35);
inline constexpr Rgba kFlow = Hex(0x1D4A70);
inline constexpr Rgba kBollard = Hex(0x55658A);
// A district served: its lamps lit, the warm light on its roofs.
inline constexpr Rgba kLampLit = Hex(0xFFF0C8), kLampGlowLit = Hex(0xFFC870), kWarm = Hex(0xFFB347);

}  // namespace threadlock::canal::color
