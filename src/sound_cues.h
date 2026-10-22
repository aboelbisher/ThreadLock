#pragma once

#include <vector>

#include "audio_synth.h"
#include "threadlock_rules.h"

// What one tick of a run sounds like (DESIGN.md §9), as plain data: which sounds to play, how loud,
// at what pitch and where, and which vibrations. Plain C++ (no engine), so it's tested on the PC;
// Feedback plays the result.
//
// The world's sound set picks the voices (audio_synth.h). On World 1 (Lantern) each pane of the rose
// window that lights rings a chime, climbing round the window as the run goes, and a drop reaching a
// drip fall's funnel plinks. On World 2 (Shop) the lazy susan clacks a notch each turn, a spill splashes
// on the counter and an order short at the end knocks dully (one sound for the run). On World 3 (Canal) a
// mill clunks and its gates clank a tick later, when their leaves swing; a short harbour's horn sags. On a tall board
// the listener is the camera: events off the view are softer, failures never.
namespace threadlock {

struct Listener {
  float top = 0.0f;      // the view's top, board units
  float visible = 1e9f;  // how much of the board it shows (the default hears everything at full volume)
};

struct Cue {
  synth::Sound sound = synth::Sound::Tap;
  float volume = 1.0f;
  float pitch = 1.0f;
  float pan = 0.0f;
  int ticks_later = 0;  // held back this many ticks (a canal gate's clank waits for its leaves to swing)
};

struct Buzz {
  int milliseconds = 0;
  float strength = 0.0f;
};

struct Cues {
  std::vector<Cue> sounds;
  std::vector<Buzz> buzzes;
};

// How loud an event at board height `y` is heard: 1 inside the view, down to 0.35 a whole view away.
float ListenerGain(const Listener& listener, float y);

// The sounds and vibrations for one tick's `events` (the board is after that tick).
Cues CuesFor(const std::vector<Event>& events, const Level& level, const Board& board, synth::SoundSet set,
             const Listener& listener = {});

// The cues held for later ticks. Tick takes one tick's cues and returns the ones to play now: those due
// (held from earlier ticks) and the new ones that aren't held. Clear drops what's waiting (a run stopped or
// restarted, ticks stepped quietly).
class CueDelay {
 public:
  std::vector<Cue> Tick(const std::vector<Cue>& cues);
  void Clear() { waiting_.clear(); }
  size_t Waiting() const { return waiting_.size(); }

 private:
  std::vector<Cue> waiting_;
};

}  // namespace threadlock
