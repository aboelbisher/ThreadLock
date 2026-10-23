#include "feedback.h"

#include <algorithm>
#include <chrono>
#include <string>

#include "wlw/core/log.h"
#include "wlw/platform/platform.h"

namespace threadlock {

namespace {

using Clock = std::chrono::steady_clock;

double MsSince(Clock::time_point start) {
  return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

const char* SetName(synth::SoundSet set) {
  switch (set) {
    case synth::SoundSet::Workshop: return "Workshop";
    case synth::SoundSet::Lantern: return "Lantern";
    case synth::SoundSet::Shop: return "Shop";
    case synth::SoundSet::Canal: return "Canal";
    case synth::SoundSet::Sanctum: return "Sanctum";
  }
  return "?";
}

}  // namespace

void Feedback::Load(wlw::audio::Audio& audio) {
  audio_ = &audio;
  ids_.fill(wlw::audio::Audio::kNoSound);
  const uint32_t rate = audio.GetMixer().SampleRate();
  const auto start = Clock::now();
  // The Workshop's now (the UI's taps and the fallback voices); the worlds' on the worker, in map order.
  size_t now = 0;
  for (size_t i = 0; i < kSounds; ++i) {
    const auto sound = static_cast<synth::Sound>(i);
    if (synth::SetOf(sound) != synth::SoundSet::Workshop) continue;
    Add(i, synth::Make(sound, rate));
    ++now;
  }
  audio.GetMixer().max_instances_per_sound = 3;
  ambience_ = std::make_unique<AmbienceStream>(rate);
  WLW_LOG_INFO("Synthesized %zu sounds at %u Hz in %.1f ms (the worlds' %zu on a worker)", now, rate, MsSince(start),
               kSounds - now);
  {
    const std::lock_guard<std::mutex> lock(queue_mutex_);
    queue_ = {synth::SoundSet::Lantern, synth::SoundSet::Shop, synth::SoundSet::Canal, synth::SoundSet::Sanctum};
  }
  Request(set_);
  worker_ = std::thread([this, rate] { Work(rate); });
}

Feedback::~Feedback() {
  stop_ = true;
  if (worker_.joinable()) worker_.join();
}

void Feedback::Work(uint32_t rate) {
  for (;;) {
    synth::SoundSet set;
    {
      const std::lock_guard<std::mutex> lock(queue_mutex_);
      if (queue_.empty()) return;
      set = queue_.front();
      queue_.pop_front();
    }
    const auto start = Clock::now();
    size_t count = 0;
    for (size_t i = 0; i < kSounds; ++i) {
      if (stop_) return;
      const auto sound = static_cast<synth::Sound>(i);
      if (synth::SetOf(sound) != set) continue;
      made_[i] = synth::Make(sound, rate);
      ready_[i].store(true, std::memory_order_release);  // made_[i] is the game thread's from here
      ++count;
    }
    WLW_LOG_INFO("Synthesized %zu %s sounds in %.1f ms (worker)", count, SetName(set), MsSince(start));
  }
}

void Feedback::Request(synth::SoundSet set) {
  const std::lock_guard<std::mutex> lock(queue_mutex_);
  const auto it = std::find(queue_.begin(), queue_.end(), set);
  if (it == queue_.end() || it == queue_.begin()) return;
  queue_.erase(it);
  queue_.push_front(set);
}

void Feedback::SetSoundSet(synth::SoundSet set) {
  set_ = set;
  Request(set);
}

void Feedback::Add(size_t sound, const std::vector<float>& mono) {
  wlw::audio::SoundData data;
  data.frames = static_cast<uint32_t>(mono.size());
  data.samples.resize(mono.size() * 2);
  for (size_t k = 0; k < mono.size(); ++k) data.samples[2 * k] = data.samples[2 * k + 1] = mono[k];
  ids_[sound] = audio_->GetMixer().AddSound(std::move(data));
}

bool Feedback::Ready(synth::Sound sound) const {
  return ids_[static_cast<size_t>(sound)] != wlw::audio::Audio::kNoSound;
}

void Feedback::Play(synth::Sound sound, float volume, float pitch, float pan) {
  if (!sound_on_ || !audio_ || !Ready(sound)) return;  // not made yet: only in the first second or so
  audio_->Play(ids_[static_cast<size_t>(sound)],
               {.volume = volume, .pitch = pitch, .pan = std::clamp(pan, -1.0f, 1.0f)});
}

void Feedback::Vibrate(int milliseconds, float strength) {
  if (haptics_on_) wlw::platform::Vibrate(milliseconds, strength);
}

void Feedback::OnEvents(const std::vector<Event>& events, const Level& level, const Board& board,
                        const Listener& listener) {
  const Cues cues = CuesFor(events, level, board, set_, listener);
  for (const Cue& cue : delay_.Tick(cues.sounds)) Play(cue.sound, cue.volume, cue.pitch, cue.pan);
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
  // The worker's sounds to the mixer (game thread only): a few a frame, at most ~2 ms of copying.
  const auto start = Clock::now();
  int handed = 0;
  for (size_t i = 0; i < kSounds && handed < 6; ++i) {
    if (ids_[i] != wlw::audio::Audio::kNoSound || !ready_[i].load(std::memory_order_acquire)) continue;
    Add(i, made_[i]);
    std::vector<float>().swap(made_[i]);
    ++handed;
    if (MsSince(start) > 2.0) break;
  }
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
  stop_ = true;  // the worker stops after the sound it's making
  if (worker_.joinable()) worker_.join();
  if (audio_ && ambience_used_) audio_->Suspend();
}

}  // namespace threadlock
