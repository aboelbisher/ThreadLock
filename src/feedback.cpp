#include "feedback.h"

#include <algorithm>
#include <chrono>

#include "wlw/core/log.h"
#include "wlw/platform/platform.h"

namespace threadlock {

void Feedback::Load(wlw::audio::Audio& audio) {
  audio_ = &audio;
  const uint32_t rate = audio.GetMixer().SampleRate();
  const auto start = std::chrono::steady_clock::now();
  for (size_t i = 0; i < ids_.size(); ++i) {
    const std::vector<float> mono = synth::Make(static_cast<synth::Sound>(i), rate);
    wlw::audio::SoundData data;
    data.frames = static_cast<uint32_t>(mono.size());
    data.samples.resize(mono.size() * 2);
    for (size_t k = 0; k < mono.size(); ++k) data.samples[2 * k] = data.samples[2 * k + 1] = mono[k];
    ids_[i] = audio.GetMixer().AddSound(std::move(data));
  }
  audio.GetMixer().max_instances_per_sound = 3;
  ambience_ = std::make_unique<AmbienceStream>(rate);
  const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
  WLW_LOG_INFO("Synthesized %zu sounds at %u Hz in %.1f ms", ids_.size(), rate, ms);
}

void Feedback::Play(synth::Sound sound, float volume, float pitch, float pan) {
  if (!sound_on_ || !audio_) return;
  audio_->Play(ids_[static_cast<size_t>(sound)],
               {.volume = volume, .pitch = pitch, .pan = std::clamp(pan, -1.0f, 1.0f)});
}

void Feedback::Vibrate(int milliseconds, float strength) {
  if (haptics_on_) wlw::platform::Vibrate(milliseconds, strength);
}

void Feedback::OnEvents(const std::vector<Event>& events, const Level& level, const Board& board,
                        const Listener& listener) {
  const Cues cues = CuesFor(events, level, board, set_, listener);
  for (const Cue& cue : cues.sounds) Play(cue.sound, cue.volume, cue.pitch, cue.pan);
  for (const Buzz& buzz : cues.buzzes) Vibrate(buzz.milliseconds, buzz.strength);
}

void Feedback::SetAmbience(bool on, bool running, Ambience::Bed bed) {
  ambience_bed_ = bed;
  ambience_wanted_ = on;
  ambience_running_ = running;
}

void Feedback::SetSettings(bool sound, bool haptics, bool music) {
  sound_on_ = sound;
  haptics_on_ = haptics;
  music_on_ = music;
  if (audio_) audio_->SetBusVolume(wlw::audio::Bus::Music, sound && music ? 1.0f : 0.0f);
}

void Feedback::Update() {
  if (!audio_ || !ambience_) return;
  // Muted = not rendered at all (no audio-thread work for nothing).
  const bool play = ambience_wanted_ && sound_on_ && music_on_;
  if (play) {
    ambience_->ambience.SetBed(ambience_bed_);
    ambience_->ambience.SetTarget(ambience_running_ ? 0.6f : 1.0f, 0.8f);
  }
  if (play == ambience_playing_) return;
  ambience_playing_ = play;
  ambience_used_ = ambience_used_ || play;
  audio_->GetMixer().PlayStream(play ? ambience_.get() : nullptr, play ? 2.0f : 1.0f);
}

void Feedback::Shutdown() {
  if (audio_ && ambience_used_) audio_->Suspend();
}

}  // namespace threadlock
