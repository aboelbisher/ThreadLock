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
// World 3, the Canal City, is a late-night jazz set: blue = a vibraphone, amber = a plucked upright bass,
// green = a Rhodes, chords are 6/9 voicings (C6/9 is the pentatonic itself: nothing clashes), the locks' pours
// a brush on a snare; a served harbour is one baked sound timed like its light (barges.h: the horn as the
// lamps come on, the chord, a brushed cymbal swelling with the district's light), a gate clanks when its
// leaves swing (a tick after the mill), murk is two hulls bumping, a gridlock horns over the grinding stop.
// World 4, the Sanctum, is a choir, a harp and an organ: blue = a concert harp, amber = a choir singing "ah", green =
// an organ's 8' flute, rose = a singing bowl, pearl = a bright harp with a choir shimmer; chords are open fifths + sus2
// (inside the pentatonic on C or G); a full phial is one baked sound timed like its elixir and ignite; a mix is a
// choir swell in the colour made; twin seals ring a low bell as they open; THREADLOCKED is the seal cracking over a
// dissonant organ drone and the grinding stop.
namespace threadlock::synth {

enum class SoundSet : uint8_t { Workshop, Lantern, Shop, Canal, Sanctum };

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
  // The Canal set (World 3).
  VibesNote,     // blue: a vibraphone, C4, its motor's tremolo on
  BassNote,      // amber: a plucked upright bass, C3
  RhodesNote,    // green: a Rhodes, C4 (the tine and its quick bell)
  HarbourBlue,   // a harbour served: the horn as its lamps come on, a vibes 6/9, a brushed cymbal with the light
  HarbourAmber,  // the same on the bass, the horn a fifth up
  HarbourGreen,  // the same on the Rhodes, the horn a tone up
  LockBrush,     // a lock's pour: a brush on a snare (the locks' beats are the groove)
  MillClunk,     // a mill's wheel turns a notch: a wooden clunk and an iron pawl
  GateClank,     // a gate's leaves swing (pitched up open, down shut)
  Murk,          // two hulls bump, the water churns murky
  Wash,          // a harbour runs over: the wake slaps the quay
  ShortQuay,     // a harbour short at the end: a lonely horn sagging, two falling bass notes
  Gridlock,      // THREADLOCKED: horns honking into each other over the grinding stop
  LastOrders,    // solved: a vibes run, the bass walking, brushes, the 6/9
  RushHour,      // 3-15 solved: horns answer across the city as its light runs, the full chord at kCityLength
  // The Sanctum set (World 4).
  HarpNote,     // blue: a concert harp, C4
  ChoirNote,    // amber: a choir singing "ah", C4 (a short swell)
  OrganNote,    // green: a pipe organ's 8' flute, C4 (its chiff as the pipe speaks)
  BowlNote,     // rose: a singing bowl, C5 (its partials beating)
  PearlNote,    // pearl: a bright harp and a celesta, a choir shimmer over them, C5
  PhialBlue,    // a phial full: a glass ring + the harp's chord as the elixir rises, a choir swell with the ignite
  PhialAmber,   // the same sung (G)
  PhialGreen,   // the same on the organ (C)
  PhialRose,    // the same on bowls (G)
  PhialPearl,   // the same on harp + celesta (C, an octave up)
  MixSwell,     // a mixer joins a pair: a choir "ah" swelling (pitched by the colour made)
  SealBell,     // twin seals open together: a low bell, the brass leaves' shimmer
  FlaskPour,    // a flask's beat: a soft glass tick and a trickle
  Clot,         // sludge: a thick low smear, bubbles popping (the stain's 1.4 s)
  SealCrack,    // THREADLOCKED: a seal cracking, a dissonant organ drone over the grinding stop
  PhialSpill,   // a phial runs over: a splash at the glass brim, drips on the table
  HollowPhial,  // a phial short at the end: a hollow glass knock, two falling harp notes
  Amen,         // solved: the organ's plagal cadence (IV-I) and the choir's "a-men" as the sigil ignites
  GreatWork,    // 4-15 solved: organ swell, the choir joining, a harp glissando, the full chord at sigil::kGrandLength
  Kindle,       // the map: a candle catching (a soft whoosh)
  Count,
};

// A mono sound at `sample_rate`, -1..1.
std::vector<float> Make(Sound sound, uint32_t sample_rate);
const char* SoundName(Sound sound);  // "HarmonicaNote" (the tool, tests)
// The set a sound is made for (by its place in Sound): the Workshop's include the UI's, the Shop's and the Sanctum's
// the map's market square and sanctum sounds. The game makes the Workshop's at startup and the others in the
// background.
SoundSet SetOf(Sound sound);

// The note for a colour (its instrument in the set), the chord a full cup rings, and the pitch for
// the k-th drop in a cup (a major pentatonic scale, climbing). Colours a set has no voice for use
// the Workshop's.
Sound NoteFor(SoundSet set, Color color);
Sound ChordFor(SoundSet set, Color color);
Sound MudFor(SoundSet set);
Sound PourFor(SoundSet set);
Sound SolvedFor(SoundSet set, bool grand);  // the payoff (grand = a two-screen finale)
Sound JamFor(SoundSet set);                 // THREADLOCKED
Sound SpillFor(SoundSet set);               // a cup runs over
Sound ShortFor(SoundSet set);               // a cup short at the end of a run; Sound::Count = no sound
Sound TurnFor(SoundSet set);                // a rack turns a notch
Sound MillFor(SoundSet set);                // a mill's drop leaves (the wheel turns)
Sound GateFor(SoundSet set);                // a gate flips
// What a mixer joining a pair plays. The Sanctum sings the colour it made (MixSwell: green 1.0, pearl a fifth up, a
// colour mixed with itself 1.0) or clots (a mud mix: Clot). Every other set plays the Workshop's green note at 0.75 for
// every mix, as before.
struct MixVoice {
  Sound sound;
  float pitch;
};
MixVoice MixFor(SoundSet set, Color made);
inline Sound NoteFor(Color color) { return NoteFor(SoundSet::Workshop, color); }
inline Sound ChordFor(Color color) { return ChordFor(SoundSet::Workshop, color); }
float ClimbPitch(int step);

// When the j-th of the heart's 12 panes starts to light (seconds after the first): LanternArt eases
// each pane at 3.5/s and starts pane j when the first reaches j/12. Heart and RoseWindow chime then.
float HeartOnset(int j);

// Amen's "-men" (the I chord), in seconds after the last phial filled. Its 1.3 s end as the whole sigil is lit
// (sigil::kSolvedLength = 3.3).
inline constexpr float kAmenFinal = 2.0f;

}  // namespace threadlock::synth
