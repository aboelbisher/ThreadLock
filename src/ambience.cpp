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
// The canal: the water, the brushes, the plucked bass (harmonics 1..4, the high ones dying first) and the far horn
// (D3 and C3 in turn, a reed's harmonics, already dulled by the distance).
constexpr float kWaterGain = 0.045f, kTrickleGain = 0.012f, kBrushGain = 0.008f, kBassGain = 0.05f, kHornGain = 0.03f;
constexpr float kBassAmp[] = {1.0f, 0.8f, 0.5f, 0.3f};
constexpr float kBassDecay[] = {1.1f, 0.7f, 0.4f, 0.25f};
constexpr float kHornNotes[] = {146.83f, 130.81f};
constexpr float kHornAmp[] = {1.0f, 0.6f, 0.35f, 0.2f, 0.1f};
constexpr float kHornLength = 2.6f;
// The sanctum: the organ's pedal (harmonics 1..4: a phone plays C2 + G2 from their 2nd-4th), the candles' crackle, a
// far choir ("ah" on G3 + D4, then A3 + E4: harmonic weights for those singers' first two formants), a low bell (C3
// then G2, ringing twice as long as the shop's).
constexpr float kDroneNotes[] = {65.41f, 98.0f}, kDroneAmp[] = {1.0f, 0.75f};
constexpr float kDroneHarm[] = {1.0f, 0.6f, 0.35f, 0.2f};
constexpr float kDroneGain = 0.016f, kCrackleGain = 0.03f, kCrackleRate = 3.5f, kChoirGain = 0.012f,
                kBreathGain = 0.03f;
constexpr float kChoirNotes[2][2] = {{196.0f, 293.66f}, {220.0f, 329.63f}};
constexpr float kChoirHarm[] = {0.25f, 0.3f, 0.45f, 0.8f, 0.5f, 0.45f};
constexpr float kChoirLength = 6.0f;
constexpr float kLowBellNotes[] = {130.81f, 98.0f};
constexpr Ambience::FarBell kShopBell{Ambience::kBellSeconds, kBellFirst, kBellRing, kBellGain, 0.7f, 1.0f, kBellNotes};
constexpr Ambience::FarBell kLowBell{
    Ambience::kLowBellSeconds, Ambience::kLowBellFirst, 7.0f, 0.07f, 0.85f, 2.0f, kLowBellNotes};

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

// A far bell at `frame` (silent between rings), from off to the left: softer on the right.
float Ambience::Bell(uint64_t frame, int channel, const FarBell& bell) const {
  const uint64_t period = static_cast<uint64_t>(bell.seconds * static_cast<float>(rate_));
  const uint64_t shifted = frame + period - static_cast<uint64_t>(bell.first * static_cast<float>(rate_));
  const double t = static_cast<double>(shifted % period) / static_cast<double>(rate_);
  if (t >= bell.ring) return 0.0f;
  const float note = bell.notes[(shifted / period) % 2];
  const float attack = std::min(1.0f, static_cast<float>(t) / 0.004f);
  const float fade = 1.0f - Smooth((static_cast<float>(t) - (bell.ring - 0.5f)) / 0.5f);  // nothing cut short
  float v = 0.0f;
  for (int k = 0; k < 5; ++k) {
    const double turns = t * static_cast<double>(note * kBellRatio[k]);
    v += kBellAmp[k] * Sine(static_cast<float>(turns - std::floor(turns))) *
         std::exp(-static_cast<float>(t) / (kBellDecay[k] * bell.decay_scale));
  }
  return bell.gain * (channel == 0 ? 1.0f : bell.right) * attack * fade * v;
}

// The canal's bass, `into` the chord: its root plucked as the chord starts, its fifth halfway through.
float Ambience::Bass(float into, const float* notes) const {
  const bool fifth = into >= 0.5f * kChordSeconds;
  const float t = fifth ? into - 0.5f * kChordSeconds : into;
  const float note = fifth ? notes[1] : notes[0];
  float v = 0.0f;
  for (int k = 0; k < 4; ++k) {
    v += kBassAmp[k] * Sine(Wrap(note * static_cast<float>(k + 1) * t)) * std::exp(-t / kBassDecay[k]);
  }
  return kBassGain * std::min(1.0f, t / 0.006f) * v;
}

// The far horn at `frame` (silent between calls): a slow swell, held, let go.
float Ambience::Horn(uint64_t frame) const {
  const uint64_t period = static_cast<uint64_t>(kHornSeconds * static_cast<float>(rate_));
  const uint64_t shifted = frame + period - static_cast<uint64_t>(kHornFirst * static_cast<float>(rate_));
  const double t = static_cast<double>(shifted % period) / static_cast<double>(rate_);
  if (t >= kHornLength) return 0.0f;
  const float note = kHornNotes[(shifted / period) % 2];
  const float s = static_cast<float>(t);
  const float env = Smooth(s / 0.4f) * (1.0f - Smooth((s - (kHornLength - 0.9f)) / 0.9f));
  float v = 0.0f;
  for (int k = 0; k < 5; ++k) {
    const double turns = t * static_cast<double>(note) * static_cast<double>(k + 1);
    v += kHornAmp[k] * Sine(static_cast<float>(turns - std::floor(turns)));
  }
  return kHornGain * env * v;
}

// The sanctum's far choir at `frame` (silent between breaths): a swell up and a long fade; `env` = how loud it is.
float Ambience::Choir(uint64_t frame, float& env) const {
  const uint64_t period = static_cast<uint64_t>(kChoirSeconds * static_cast<float>(rate_));
  const uint64_t shifted = frame + period - static_cast<uint64_t>(kChoirFirst * static_cast<float>(rate_));
  const double t = static_cast<double>(shifted % period) / static_cast<double>(rate_);
  env = 0.0f;
  if (t >= kChoirLength) return 0.0f;
  const float s = static_cast<float>(t);
  env = Smooth(s / 2.5f) * (1.0f - Smooth((s - 3.0f) / 3.0f));
  const float* notes = kChoirNotes[(shifted / period) % 2];
  float v = 0.0f;
  for (int n = 0; n < 2; ++n) {
    for (int h = 0; h < 6; ++h) {
      const double turns = t * static_cast<double>(notes[n]) * static_cast<double>(h + 1);
      v += kChoirHarm[h] * Sine(static_cast<float>(turns - std::floor(turns)));
    }
  }
  return kChoirGain * env * v;
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
  const float k_water = 2.0f * std::sin(std::numbers::pi_v<float> * 320.0f / rate);
  const float k_trickle = 2.0f * std::sin(std::numbers::pi_v<float> * 1500.0f / rate);
  const float k_brush = 2.0f * std::sin(std::numbers::pi_v<float> * 3800.0f / rate);
  const float k_breath = 2.0f * std::sin(std::numbers::pi_v<float> * 1100.0f / rate);  // the choir's breath
  const float k_crackle = 2.0f * std::sin(std::numbers::pi_v<float> * 2000.0f / rate);
  const float crackle_fall = std::exp(-1.0f / (0.002f * rate));
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
        trickle_low_[0] = trickle_low_[1] = trickle_band_[0] = trickle_band_[1] = 0.0f;
        brush_low_[0] = brush_low_[1] = brush_band_[0] = brush_band_[1] = 0.0f;
        crackle_low_[0] = crackle_low_[1] = crackle_band_[0] = crackle_band_[1] = crackle_[0] = crackle_[1] = 0.0f;
      }
    } else if (switch_ < 1.0f) {
      switch_ = std::min(1.0f, switch_ + switch_step);
    }
    const bool shop = bed_ == Bed::Shop, canal = bed_ == Bed::Canal, sanctum = bed_ == Bed::Sanctum;
    const uint64_t chord = frame_ / chord_frames;
    const float into = static_cast<float>(frame_ - chord * chord_frames) / rate;
    float out[2] = {0.0f, 0.0f};
    // Phases: [layer][voice][channel]; chords alternate between two layers, so the one fading out
    // keeps its phases while the next fades in.
    for (int age = 0; age < 2 && !canal && !sanctum; ++age) {  // the chord and the one before (canal, sanctum: no pad)
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
    // The canal: the bass and the far horn (from down the canal on the left), the brush's swirl (round once
    // a second, a little push on each beat).
    float canal_mono = 0.0f, swirl = 0.0f;
    if (canal) {
      canal_mono = Bass(into, kChords[chord % 4]) + Horn(frame_);
      const double second = static_cast<double>(frame_) / static_cast<double>(rate_);
      const float round = 0.5f + 0.5f * Sine(static_cast<float>(second - std::floor(second)));
      const double beats = 2.0 * second;
      const float push = 0.5f + 0.5f * Sine(static_cast<float>(beats - std::floor(beats)));
      swirl = 0.3f + 0.4f * round + 0.3f * push * push * push * push;
    }
    // The sanctum: the far choir (one voice for both sides) and the drone's slow breathing.
    float choir_env = 0.0f, sanctum_mono = 0.0f, drone_swell = 0.0f;
    if (sanctum) {
      sanctum_mono = Choir(frame_, choir_env);
      const double turns16 = static_cast<double>(frame_) / (2.0 * kDroneSwell * static_cast<double>(rate_));
      drone_swell = 0.6f + 0.4f * Sine(static_cast<float>(turns16 - std::floor(turns16)));
    }
    const float k = canal ? k_water : shop ? k_street : sanctum ? k_breath : k_air;
    for (int ch = 0; ch < 2; ++ch) {
      noise_[ch] = noise_[ch] * 1664525u + 1013904223u;
      const float n = static_cast<float>(noise_[ch] >> 8) / 8388608.0f - 1.0f;
      low_[ch] += k * band_[ch];
      band_[ch] += k * (n - low_[ch] - (shop || canal ? 0.9f : sanctum ? 0.6f : 1.2f) * band_[ch]);
      if (canal) {
        // The water laps in two uneven slow rhythms (different on each side), a trickle riding the slaps.
        const double t = static_cast<double>(frame_) / static_cast<double>(rate_);
        const double la = t * 0.43 + 0.37 * ch, lb = t * 0.29 + 0.61 * ch + 0.2;
        const float a = std::max(0.0f, Sine(static_cast<float>(la - std::floor(la))));
        const float b = std::max(0.0f, Sine(static_cast<float>(lb - std::floor(lb))));
        const float lap = 0.2f + 0.8f * std::min(1.0f, 0.7f * a * a * a + 0.5f * b * b * b);
        trickle_low_[ch] += k_trickle * trickle_band_[ch];
        trickle_band_[ch] += k_trickle * (n - trickle_low_[ch] - 0.6f * trickle_band_[ch]);
        brush_low_[ch] += k_brush * brush_band_[ch];
        brush_band_[ch] += k_brush * (n - brush_low_[ch] - 0.7f * brush_band_[ch]);
        out[ch] += kWaterGain * lap * band_[ch] + kTrickleGain * lap * lap * trickle_band_[ch] +
                   kBrushGain * swirl * brush_band_[ch] + (ch == 0 ? 1.0f : 0.8f) * canal_mono;
      } else if (sanctum) {
        // The pedal: C2 and G2, the right a hair sharp (the pad's phases are free in this bed).
        float drone = 0.0f;
        for (int j = 0; j < 2; ++j) {
          float& phase = phase_[static_cast<size_t>(2 * j + ch)];
          phase = Wrap(phase + kDroneNotes[j] * (ch ? 1.0015f : 1.0f) / rate);
          float v = 0.0f;
          for (int h = 0; h < 4; ++h) v += kDroneHarm[h] * Sine(Wrap(static_cast<float>(h + 1) * phase));
          drone += kDroneAmp[j] * v;
        }
        // The candles: now and then a crackle, dying in ~2 ms, through a band at 2 kHz.
        crackle_noise_ = crackle_noise_ * 1664525u + 1013904223u;
        const float u = static_cast<float>(crackle_noise_ >> 8) / 16777216.0f;
        if (u < kCrackleRate / rate) {
          crackle_noise_ = crackle_noise_ * 1664525u + 1013904223u;
          crackle_[ch] = 0.4f + 0.6f * static_cast<float>(crackle_noise_ >> 8) / 16777216.0f;
        } else {
          crackle_[ch] *= crackle_fall;
        }
        crackle_low_[ch] += k_crackle * crackle_band_[ch];
        crackle_band_[ch] += k_crackle * (crackle_[ch] * n - crackle_low_[ch] - 0.9f * crackle_band_[ch]);
        out[ch] += kDroneGain * drone_swell * drone + kCrackleGain * crackle_band_[ch] +
                   (ch == 0 ? 1.0f : 0.8f) * sanctum_mono + kBreathGain * choir_env * band_[ch] +
                   Bell(frame_, ch, kLowBell);
      } else if (shop) {
        flutter_[ch] += talk * (n - flutter_[ch]);
        const float voices = std::clamp(0.65f + 35.0f * flutter_[ch], 0.2f, 1.2f);
        out[ch] += kMurmurGain * swell * voices * band_[ch] + Bell(frame_, ch, kShopBell);
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
