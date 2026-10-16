#pragma once

#include <cstdint>
#include <vector>

#include "threadlock_rules.h"

// Threadlock's sounds are made in code at startup (no audio files): a few hundred milliseconds of
// arithmetic replaces megabytes in the APK, and every sound is in tune with every other.
//
// The machine plays music (DESIGN.md §10): each colour has an instrument; a drop landing plays a
// note that climbs as its cup fills; a full cup rings a chord; a tick is a sixteenth note at
// 120 BPM, so landings fall on the beat by themselves. Mud is a dull thud, a jam a grinding stop
// and a low organ chord.
namespace threadlock::synth {

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
  Count,
};

// A mono sound at `sample_rate`, -1..1.
std::vector<float> Make(Sound sound, uint32_t sample_rate);

// The note for a colour (its instrument), and the pitch for the k-th drop in a cup (a major
// pentatonic scale, climbing).
Sound NoteFor(Color color);
Sound ChordFor(Color color);
float ClimbPitch(int step);

}  // namespace threadlock::synth
