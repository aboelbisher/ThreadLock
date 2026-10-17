#include "ambience.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace threadlock {
namespace {

// Cadd9, Am7, Fmaj7, G6: low and open, the top notes inside the C pentatonic.
constexpr float kChords[4][Ambience::kVoices] = {
    {130.81f, 196.00f, 261.63f, 293.66f, 329.63f},  // C3 G3 C4 D4 E4
    {110.00f, 164.81f, 196.00f, 261.63f, 329.63f},  // A2 E3 G3 C4 E4
    {87.31f, 130.81f, 164.81f, 220.00f, 261.63f},   // F2 C3 E3 A3 C4
    {98.00f, 146.83f, 196.00f, 246.94f, 329.63f},   // G2 D3 G3 B3 E4
};
constexpr float kVoiceGain = 0.022f, kAirGain = 0.025f;
constexpr float kRise = 3.0f, kFall = 3.0f;  // a chord fades in over 3 s and out over 3 s after its 8

float Smooth(float x) {
  x = std::clamp(x, 0.0f, 1.0f);
  return x * x * (3.0f - 2.0f * x);
}

// A chord's loudness `t` seconds after it starts.
float Envelope(float t) {
  if (t < 0.0f) return 0.0f;
  if (t < kRise) return Smooth(t / kRise);
  if (t < Ambience::kChordSeconds) return 1.0f;
  return 1.0f - Smooth((t - Ambience::kChordSeconds) / kFall);
}

float Wrap(float phase) { return phase - std::floor(phase); }

}  // namespace

Ambience::Ambience(uint32_t sample_rate) : rate_(sample_rate) {
  for (int i = 0; i <= kTable; ++i) {
    table_[static_cast<size_t>(i)] =
        std::sin(2.0f * std::numbers::pi_v<float> * static_cast<float>(i) / static_cast<float>(kTable));
  }
}

float Ambience::Sine(float phase) const {
  const float x = phase * static_cast<float>(kTable);
  const int i = std::min(static_cast<int>(x), kTable - 1);
  const float f = x - static_cast<float>(i);
  return table_[static_cast<size_t>(i)] + f * (table_[static_cast<size_t>(i) + 1] - table_[static_cast<size_t>(i)]);
}

void Ambience::SetTarget(float gain, float seconds) {
  step_.store(1.0f / std::max(1.0f, seconds * static_cast<float>(rate_)));
  target_.store(std::clamp(gain, 0.0f, 1.0f));
}

void Ambience::Render(float* stereo, uint32_t frames) {
  const float target = target_.load(), step = step_.load();
  const float rate = static_cast<float>(rate_);
  const uint64_t chord_frames = static_cast<uint64_t>(kChordSeconds * rate);
  const float k = 2.0f * std::sin(std::numbers::pi_v<float> * 700.0f / rate);  // the air filter
  // Phases: [layer][voice][channel]; chords alternate between two layers, so the one fading out
  // keeps its phases while the next fades in.
  for (uint32_t i = 0; i < frames; ++i, ++frame_) {
    if (gain_ < target) gain_ = std::min(target, gain_ + step);
    if (gain_ > target) gain_ = std::max(target, gain_ - step);
    const uint64_t chord = frame_ / chord_frames;
    const float into = static_cast<float>(frame_ - chord * chord_frames) / rate;
    float out[2] = {0.0f, 0.0f};
    for (int age = 0; age < 2; ++age) {  // the current chord and the one before it
      if (age == 1 && chord == 0) break;
      const uint64_t c = chord - static_cast<uint64_t>(age);
      const float env = Envelope(into + kChordSeconds * static_cast<float>(age));
      const size_t layer = static_cast<size_t>(c % 2);
      const float* notes = kChords[c % 4];
      for (int v = 0; v < kVoices; ++v) {
        // A slow breathing per voice, and the right channel a hair sharp: the pad drifts and widens.
        const float breath = 1.0f + 0.15f * Sine(Wrap(0.11f * into + 0.23f * static_cast<float>(v)));
        for (int ch = 0; ch < 2; ++ch) {
          float& phase = phase_[(layer * kVoices + static_cast<size_t>(v)) * 2 + static_cast<size_t>(ch)];
          if (env <= 0.0f) continue;
          const float freq = notes[v] * (ch == 0 ? 1.0f : 1.0015f);
          phase = Wrap(phase + freq / rate);
          const float s = Sine(phase) + 0.15f * Sine(Wrap(2.0f * phase));
          out[ch] += kVoiceGain * env * breath * s;
        }
      }
    }
    // The air: band-passed noise, one source per channel, swelling over ~13 s.
    const double turns = static_cast<double>(frame_) / (13.0 * static_cast<double>(rate_));
    const float swell = 0.55f + 0.45f * Sine(static_cast<float>(turns - std::floor(turns)));
    for (int ch = 0; ch < 2; ++ch) {
      noise_[ch] = noise_[ch] * 1664525u + 1013904223u;
      const float n = static_cast<float>(noise_[ch] >> 8) / 8388608.0f - 1.0f;
      low_[ch] += k * band_[ch];
      band_[ch] += k * (n - low_[ch] - 1.2f * band_[ch]);
      out[ch] += kAirGain * swell * band_[ch];
    }
    stereo[2 * i] = gain_ * out[0];
    stereo[2 * i + 1] = gain_ * out[1];
  }
}

}  // namespace threadlock
