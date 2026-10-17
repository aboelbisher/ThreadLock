#pragma once

#include <array>
#include <vector>

#include "audio_synth.h"
#include "threadlock_rules.h"
#include "wlw/audio/audio.h"

// What the player hears and feels (DESIGN.md §9): the synthesized sounds (audio_synth.h) played
// for the board's events, and short vibrations: a light tap for each drop landing, a heavy one for
// mud and jams. The settings switch either off.
namespace threadlock {

class Feedback {
 public:
  void Load(wlw::audio::Audio& audio);  // synthesizes every sound (a few ms each)

  void Play(synth::Sound sound, float volume = 1.0f, float pitch = 1.0f, float pan = 0.0f);
  void Vibrate(int milliseconds, float strength);

  // Sounds and vibrations for one tick's events.
  void OnEvents(const std::vector<Event>& events, const Level& level, const Board& board);

  bool sound_on = true;
  bool haptics_on = true;

 private:
  wlw::audio::Audio* audio_ = nullptr;
  std::array<wlw::audio::Audio::SoundId, static_cast<size_t>(synth::Sound::Count)> ids_{};
};

}  // namespace threadlock
