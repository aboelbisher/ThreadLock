#pragma once

#include <cstdint>
#include <vector>

#include "threadlock_rules.h"

// Threadlock's sounds are made in code at startup (no audio files): a few hundred milliseconds of
// arithmetic replaces megabytes in the APK, and every sound is in tune with every other.
//
// The machine plays music (DESIGN.md §9): each colour has an instrument; a drop landing plays a
// note that climbs as its cup fills; a full cup rings a chord; a tick is a sixteenth note at
// 120 BPM, so landings fall on the beat by themselves. Mud is a dull thud, a jam a grinding stop
// and a low organ chord.
//
// Each world has its own set of instruments (the look picks it, world_look.h): the colour picks the
// voice inside the set. World 1, the Lantern Works, is glass and bells: blue = a glass harmonica,
// amber = a celesta, a full lantern rings tubular chimes, each window pane that lights a small
// chime that climbs as the run goes, the solved window's heart a chord bloom and twelve chimes.
// World 2, the Apothecary, is wood and brass: blue = a marimba, amber = a pizzicato string; a filled
// order is one baked sound timed like its moment on screen (apothecary_look.h: the cork drops, the shop
// bell rings, the PAID stamp thumps), mud a glop on wood, a spill a splash and drips off the counter.
namespace threadlock::synth {

enum class SoundSet : uint8_t { Workshop, Lantern, Shop };

enum class Sound : uint8_t {
  NoteBlue,
  NoteAmber,
  NoteGreen,
  NoteRose,
  NotePearl,
  ChordBlue,
  ChordAmber,
  ChordGreen,
  ChordRose,
  ChordPearl,
  Thud,     // mud
  Splash,   // a wrong colour / a spill
  Chime,    // a near miss
  Grind,    // THREADLOCKED
  Fanfare,  // solved
  Star,     // a star earned
  Pour,     // a spout's beat
  Click,    // a turnstile turning, a flipper flipping
  Place,    // a piece lands in a socket
  Lift,     // a piece picked up
  Tap,      // a setting changed, a button
  // The Lantern set (World 1).
  HarmonicaNote,  // blue: a glass harmonica, C4
  CelestaNote,    // amber: a celesta, C5
  LanternBlue,    // a full blue lantern: tubular chimes + a harmonica chord
  LanternAmber,   // a full amber lantern: the chimes a fifth up + a celesta chord
  PaneChime,      // a window pane lights (pitched by the pane: the window climbs)
  Heart,          // solved: the window's heart lights, a chord bloom + 12 chimes
  RoseWindow,     // 1-15 solved: longer, lower, a high shimmer at the end
  Smoke,          // mud: a muffled glass knock + a low hiss
  Drip,           // a drop lands in a drip fall's funnel
  CruciblePour,   // a crucible's beat
  Ascent,         // a tall board's intro pan
  // The Shop set (World 2).
  MarimbaNote,  // blue: a rosewood marimba, C4
  PizzNote,     // amber: a pizzicato string, C4
  PaidBlue,     // an order filled: cork, a marimba chord, the shop bell, the PAID stamp (at the art's times)
  PaidAmber,    // the same with a pizzicato chord, the bell a fifth up
  Sludge,       // mud: a low glop and a knock on wood
  JarTap,       // a jar's beat: a short glug
  SpillWood,    // a bottle runs over: a splash on the counter, drips off its edge
  ShortDull,    // an order short at the end: a dull knock, two falling pizzicato notes
  RackClack,    // the lazy susan turns: a soft wooden clack
  Till,         // solved: a marimba run, the till's bell, coins
  MarketDay,    // 2-15 solved: longer, marimba and pizzicato runs, the shop's bells
  // The city map's market square.
  CorkPop,   // a bottle in the shop window corked (Lean)
  ShopBell,  // the shop's door bell: a level's bottle done, the shop waking
  Count,
};

// A mono sound at `sample_rate`, -1..1.
std::vector<float> Make(Sound sound, uint32_t sample_rate);
const char* SoundName(Sound sound);  // "HarmonicaNote" (the tool, tests)

// The note for a colour (its instrument in the set), the chord a full cup rings, and the pitch for
// the k-th drop in a cup (a major pentatonic scale, climbing). Colours a set has no voice for use
// the Workshop's.
Sound NoteFor(SoundSet set, Color color);
Sound ChordFor(SoundSet set, Color color);
Sound MudFor(SoundSet set);
Sound PourFor(SoundSet set);
Sound SolvedFor(SoundSet set, bool grand);  // the payoff (grand = a two-screen finale)
inline Sound NoteFor(Color color) { return NoteFor(SoundSet::Workshop, color); }
inline Sound ChordFor(Color color) { return ChordFor(SoundSet::Workshop, color); }
float ClimbPitch(int step);

// When the j-th of the heart's 12 panes starts to light (seconds after the first): LanternArt eases
// each pane at 3.5/s and starts pane j when the first reaches j/12. Heart and RoseWindow chime then.
float HeartOnset(int j);

}  // namespace threadlock::synth
