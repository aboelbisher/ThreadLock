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
constexpr float kStringGain = 0.013f, kMurmurGain = 0.03f, kBellGain = 0.05f;
constexpr float kRise = 3.0f, kFall = 3.0f;  // a chord fades in over 3 s and out over 3 s after its 8
// The far bell: a church bell's partials (hum, prime, minor third, fifth, nominal), G5 and E5 in turn.
constexpr float kBellRatio[] = {0.5f, 1.0f, 1.19f, 1.5f, 2.0f};
constexpr float kBellAmp[] = {0.3f, 1.0f, 0.4f, 0.25f, 0.2f};
constexpr float kBellDecay[] = {2.0f, 1.6f, 1.0f, 0.6f, 0.4f};
constexpr float kBellNotes[] = {783.99f, 659.25f};
constexpr float kBellRing = 4.5f, kBellFirst = 6.0f;  // how long one rings; when the first one rings

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

// The far bell at `frame` (silent between rings), from down the street on the left: softer on the right.
float Ambience::Bell(uint64_t frame, int channel) const {
  const uint64_t period = static_cast<uint64_t>(kBellSeconds * static_cast<float>(rate_));
  const uint64_t shifted = frame + period - static_cast<uint64_t>(kBellFirst * static_cast<float>(rate_));
  const double t = static_cast<double>(shifted % period) / static_cast<double>(rate_);
  if (t >= kBellRing) return 0.0f;
  const float note = kBellNotes[(shifted / period) % 2];
  const float attack = std::min(1.0f, static_cast<float>(t) / 0.004f);
  const float fade = 1.0f - Smooth((static_cast<float>(t) - (kBellRing - 0.5f)) / 0.5f);  // nothing cut short
  float v = 0.0f;
  for (int k = 0; k < 5; ++k) {
    const double turns = t * static_cast<double>(note * kBellRatio[k]);
    v += kBellAmp[k] * Sine(static_cast<float>(turns - std::floor(turns))) *
         std::exp(-static_cast<float>(t) / kBellDecay[k]);
  }
  return kBellGain * (channel == 0 ? 1.0f : 0.7f) * attack * fade * v;
}

void Ambience::Render(float* stereo, uint32_t frames) {
  const float target = target_.load(), step = step_.load();
  const Bed wanted = wanted_.load();
  const float rate = static_cast<float>(rate_);
  const uint64_t chord_frames = static_cast<uint64_t>(kChordSeconds * rate);
  const float switch_step = 1.0f / (kSwitchSeconds * rate);
  const float talk = 2.0f * std::numbers::pi_v<float> * 4.0f / rate;               // the murmur's flutter, ~4 Hz
  const float k_air = 2.0f * std::sin(std::numbers::pi_v<float> * 700.0f / rate);  // the noise bands' filters
  const float k_street = 2.0f * std::sin(std::numbers::pi_v<float> * 420.0f / rate);
  for (uint32_t i = 0; i < frames; ++i, ++frame_) {
    if (gain_ < target) gain_ = std::min(target, gain_ + step);
    if (gain_ > target) gain_ = std::max(target, gain_ - step);
    // A new bed: the old one fades out, then the new one fades in (at once when nothing is heard).
    if (wanted != bed_) {
      switch_ = gain_ <= 0.0f ? 0.0f : switch_ - switch_step;
      if (switch_ <= 0.0f) {
        bed_ = wanted;
        switch_ = gain_ <= 0.0f ? 1.0f : 0.0f;
        low_[0] = low_[1] = band_[0] = band_[1] = 0.0f;
      }
    } else if (switch_ < 1.0f) {
      switch_ = std::min(1.0f, switch_ + switch_step);
    }
    const bool shop = bed_ == Bed::Shop;
    const uint64_t chord = frame_ / chord_frames;
    const float into = static_cast<float>(frame_ - chord * chord_frames) / rate;
    float out[2] = {0.0f, 0.0f};
    // Phases: [layer][voice][channel]; chords alternate between two layers, so the one fading out
    // keeps its phases while the next fades in.
    for (int age = 0; age < 2; ++age) {  // the current chord and the one before it
      if (age == 1 && chord == 0) break;
      const uint64_t c = chord - static_cast<uint64_t>(age);
      const float env = Envelope(into + kChordSeconds * static_cast<float>(age));
      const size_t layer = static_cast<size_t>(c % 2);
      const float* notes = kChords[c % 4];
      for (int v = 0; v < kVoices; ++v) {
        // A slow breathing per voice, and the right channel a hair sharp: the pad drifts and widens.
        const float breath = 1.0f + 0.15f * Sine(Wrap(0.11f * into + 0.23f * static_cast<float>(v)));
        // The strings' vibrato: ~5 Hz, a little different per voice.
        const float vibrato = shop ? 1.0f + 0.0025f * Sine(vibrato_[static_cast<size_t>(v)]) : 1.0f;
        for (int ch = 0; ch < 2; ++ch) {
          float& phase = phase_[(layer * kVoices + static_cast<size_t>(v)) * 2 + static_cast<size_t>(ch)];
          if (env <= 0.0f) continue;
          const float freq = notes[v] * (ch == 0 ? 1.0f : 1.0015f) * vibrato;
          phase = Wrap(phase + freq / rate);
          if (shop) {  // a bowed string: a few harmonics falling off like a saw's
            const float s = Sine(phase) + 0.45f * Sine(Wrap(2.0f * phase)) + 0.25f * Sine(Wrap(3.0f * phase));
            out[ch] += kStringGain * env * breath * s;
          } else {
            const float s = Sine(phase) + 0.15f * Sine(Wrap(2.0f * phase));
            out[ch] += kVoiceGain * env * breath * s;
          }
        }
      }
    }
    if (shop) {
      for (int v = 0; v < kVoices; ++v) {
        float& p = vibrato_[static_cast<size_t>(v)];
        p = Wrap(p + (4.8f + 0.3f * static_cast<float>(v)) / rate);
      }
    }
    // The air (Lantern) or the street's murmur (Shop): band-passed noise, one source per channel,
    // swelling over ~13 s (the street: two slow swells and a talk-like flutter).
    const double turns = static_cast<double>(frame_) / (13.0 * static_cast<double>(rate_));
    float swell = 0.55f + 0.45f * Sine(static_cast<float>(turns - std::floor(turns)));
    if (shop) {
      const double turns7 = static_cast<double>(frame_) / (7.0 * static_cast<double>(rate_));
      swell = 0.45f + 0.3f * swell + 0.25f * Sine(static_cast<float>(turns7 - std::floor(turns7)));
    }
    const float k = shop ? k_street : k_air;
    for (int ch = 0; ch < 2; ++ch) {
      noise_[ch] = noise_[ch] * 1664525u + 1013904223u;
      const float n = static_cast<float>(noise_[ch] >> 8) / 8388608.0f - 1.0f;
      low_[ch] += k * band_[ch];
      band_[ch] += k * (n - low_[ch] - (shop ? 0.9f : 1.2f) * band_[ch]);
      if (shop) {
        flutter_[ch] += talk * (n - flutter_[ch]);
        const float voices = std::clamp(0.65f + 35.0f * flutter_[ch], 0.2f, 1.2f);
        out[ch] += kMurmurGain * swell * voices * band_[ch] + Bell(frame_, ch);
      } else {
        out[ch] += kAirGain * swell * band_[ch];
      }
    }
    const float g = gain_ * Smooth(switch_);
    stereo[2 * i] = g * out[0];
    stereo[2 * i + 1] = g * out[1];
  }
}

}  // namespace threadlock
