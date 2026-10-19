#pragma once

#include <cstdint>
#include <iterator>

#include "audio_synth.h"
#include "palette.h"

// How a world looks around its machines (DESIGN.md §5): the HUD and the cards keep one layout in every
// world, tinted with the world's colours, and each world draws its boards with its own art and plays
// its own instruments. A world whose art isn't built yet uses the workshop look and sounds (v2), as do
// the daily puzzle and the Lab.
namespace threadlock {

enum class ArtKind : uint8_t { Workshop, Lantern, Apothecary };

struct WorldLook {
  ArtKind art = ArtKind::Workshop;
  const char* place = "";  // "The Lantern Works"
  Rgba background;         // behind everything
  Rgba panel;              // the tray, the round buttons
  Rgba panel_edge;
  Rgba ink;     // text
  Rgba muted;   // smaller text, star labels
  Rgba accent;  // the subtitle, free sockets, highlights
  // The big EDIT / PLAY button: a gradient from top to bottom over a darker lip.
  Rgba action_top, action_bottom, action_lip, on_action;
  synth::SoundSet sounds = synth::SoundSet::Workshop;  // its instruments (audio_synth.h)
};

inline constexpr WorldLook kWorkshopLook{ArtKind::Workshop,
                                         "The Workshop",
                                         Hex(0x070B10),
                                         Hex(0x0D141C, 0.92f),
                                         Hex(0xD4E3F2, 0.16f),
                                         Hex(0xD4E3F2),
                                         Hex(0x8FA4B8),
                                         Hex(0x9EE3B0),
                                         Hex(0xC4F2D0),
                                         Hex(0x7CCB93),
                                         Hex(0x3C7A4E),
                                         Hex(0x08140C),
                                         synth::SoundSet::Workshop};

inline constexpr WorldLook kWorldLooks[] = {
    // World 1, First Light: the glassworks at the top of the tower, at dawn.
    {ArtKind::Lantern, "The Lantern Works", Hex(0x080C15), Hex(0x0B1522, 0.86f), Hex(0xCFE6FF, 0.2f), Hex(0xF2F7FF),
     Hex(0xA9BED6), Hex(0xF6DA9A), Hex(0xFFE6A6), Hex(0xDFA74E), Hex(0x9A6A22), Hex(0x2A1A00),
     synth::SoundSet::Lantern},
    // World 2, Fair Shares: an apothecary on market day.
    {ArtKind::Apothecary, "The Apothecary", Hex(0x1A120C), Hex(0x2A1D14, 0.9f), Hex(0xF3E8CC, 0.2f), Hex(0xF3E8CC),
     Hex(0xC9AE8C), Hex(0xFFB45C), Hex(0xFFD99A), Hex(0xEE9645), Hex(0x9A5418), Hex(0x2A1600),
     synth::SoundSet::Workshop},
    // World 3, Rush Hour: the canal city at night.
    {ArtKind::Workshop, "The Canal City", Hex(0x0B111D), Hex(0x0E1A2A, 0.9f), Hex(0xA8F2EA, 0.18f), Hex(0xE6F2FF),
     Hex(0x9FB0D2), Hex(0x7FE3D8), Hex(0xA8F2EA), Hex(0x2FB5A8), Hex(0x14706A), Hex(0x06201E),
     synth::SoundSet::Workshop},
    // World 4, The Mixing Hall: the alchemists' sanctum.
    {ArtKind::Workshop, "The Sanctum", Hex(0x100C17), Hex(0x1A1426, 0.9f), Hex(0xDCC8FF, 0.18f), Hex(0xF2EEFF),
     Hex(0xB4A6D6), Hex(0xE6CC88), Hex(0xDCC8FF), Hex(0x9A6CF2), Hex(0x5A3AA8), Hex(0x1A0E33),
     synth::SoundSet::Workshop},
};

// `world` counts from 1; anything else (the daily, the Lab) gets the workshop look.
inline const WorldLook& LookForWorld(int world) {
  if (world >= 1 && world <= static_cast<int>(std::size(kWorldLooks))) return kWorldLooks[world - 1];
  return kWorkshopLook;
}

}  // namespace threadlock
