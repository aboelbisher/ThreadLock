#pragma once

#include <array>
#include <memory>
#include <vector>

#include "ambience.h"
#include "audio_synth.h"
#include "sound_cues.h"
#include "threadlock_rules.h"
#include "wlw/audio/audio.h"

// What the player hears and feels (DESIGN.md §9): the synthesized sounds (audio_synth.h) played
// for the board's events (sound_cues.h), short vibrations (a light tap for each drop landing, a
// heavy one for mud and jams), and the worlds' ambience on the Music bus. The settings switch each off.
namespace threadlock {

class Feedback {
 public:
  void Load(wlw::audio::Audio& audio);  // synthesizes every sound (a few ms each)

  void Play(synth::Sound sound, float volume = 1.0f, float pitch = 1.0f, float pan = 0.0f);
  void Vibrate(int milliseconds, float strength);

  // The world's instruments (its look picks them).
  void SetSoundSet(synth::SoundSet set) { set_ = set; }
  synth::SoundSet GetSoundSet() const { return set_; }

  // Sounds and vibrations for one tick's events, heard from the camera on a tall board (a cue held for later
  // ticks plays on a later call: one call per tick).
  void OnEvents(const std::vector<Event>& events, const Level& level, const Board& board,
                const Listener& listener = {});
  void ClearPending() { delay_.Clear(); }  // a run stopped, restarted or stepped quietly: held cues go

  // The ambience bed: on while a level that has one is open, ducked to 60% while a run plays. Applied
  // once a frame by Update, so leaving one level for the next doesn't stop and restart it.
  void SetAmbience(bool on, bool running, Ambience::Bed bed = Ambience::Bed::Lantern);
  void Update();  // ThreadlockApp::OnUpdate

  // The settings (ThreadlockApp::ApplySettings); music = the ambience (Bus::Music).
  void SetSettings(bool sound, bool haptics, bool music);

  // At the app's end, before the systems go (the Audio outlives them): stops the device, so the mixer
  // never reads the ambience after it's gone.
  void Shutdown();

 private:
  // The ambience as the mixer's stream: Read runs on the audio thread.
  struct AmbienceStream : wlw::audio::StreamSource {
    explicit AmbienceStream(uint32_t rate) : ambience(rate) {}
    uint32_t Read(float* stereo, uint32_t frames) override {
      ambience.Render(stereo, frames);
      return frames;
    }
    Ambience ambience;
  };

  wlw::audio::Audio* audio_ = nullptr;
  std::array<wlw::audio::Audio::SoundId, static_cast<size_t>(synth::Sound::Count)> ids_{};
  synth::SoundSet set_ = synth::SoundSet::Workshop;
  CueDelay delay_;
  bool sound_on_ = true, haptics_on_ = true, music_on_ = true;
  std::unique_ptr<AmbienceStream> ambience_;  // never freed while the mixer may read it (lives with the app)
  Ambience::Bed ambience_bed_ = Ambience::Bed::Lantern;
  bool ambience_wanted_ = false, ambience_running_ = false, ambience_playing_ = false, ambience_used_ = false;
};

}  // namespace threadlock
