#include "audio_synth.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace threadlock::synth {
namespace {

constexpr float kTau = 2.0f * std::numbers::pi_v<float>;
constexpr float kC4 = 261.63f, kC5 = 523.25f, kC6 = 1046.5f;

struct Buffer {
  uint32_t rate;
  std::vector<float> samples;

  Buffer(uint32_t sample_rate, float seconds)
      : rate(sample_rate), samples(static_cast<size_t>(seconds * static_cast<float>(sample_rate)), 0.0f) {}
  float T(size_t i) const { return static_cast<float>(i) / static_cast<float>(rate); }
  size_t At(float seconds) const {
    return std::min(samples.size(), static_cast<size_t>(seconds * static_cast<float>(rate)));
  }
};

// A small deterministic noise source (the same sounds on every device).
struct Noise {
  uint32_t state = 0x12345678u;
  float Next() {
    state = state * 1664525u + 1013904223u;
    return static_cast<float>(state >> 8) / 8388608.0f - 1.0f;
  }
};

float Decay(float t, float attack, float time) { return t < attack ? t / attack : std::exp(-(t - attack) / time); }

// One instrument note at `freq`, added into `out` from `start` seconds, scaled by `gain`.
void Instrument(Color color, float freq, Buffer& out, float start, float gain) {
  const size_t from = out.At(start);
  switch (color) {
    case Color::Blue: {  // marimba: a struck bar's inharmonic partials, the high ones die first
      const float ratio[] = {1.0f, 3.93f, 9.0f}, amp[] = {1.0f, 0.32f, 0.08f}, time[] = {0.38f, 0.09f, 0.03f};
      for (size_t i = from; i < out.samples.size(); ++i) {
        const float t = out.T(i - from);
        float v = 0.0f;
        for (int k = 0; k < 3; ++k) v += amp[k] * std::sin(kTau * freq * ratio[k] * t) * Decay(t, 0.002f, time[k]);
        out.samples[i] += gain * v;
      }
      break;
    }
    case Color::Amber: {  // a plucked string (Karplus-Strong): a burst of noise in a feedback delay
      Noise noise;
      const size_t period = std::max<size_t>(2, static_cast<size_t>(static_cast<float>(out.rate) / freq));
      std::vector<float> line(period);
      for (float& s : line) s = noise.Next();
      size_t pos = 0;
      float last = 0.0f;
      for (size_t i = from; i < out.samples.size(); ++i) {
        const float current = line[pos];
        const float next = 0.5f * (current + last) * 0.996f;
        last = current;
        line[pos] = next;
        pos = (pos + 1) % period;
        out.samples[i] += gain * 0.8f * current * Decay(out.T(i - from), 0.001f, 0.6f);
      }
      break;
    }
    case Color::Green: {  // glass: FM with an inharmonic ratio, the brightness fading fast
      for (size_t i = from; i < out.samples.size(); ++i) {
        const float t = out.T(i - from);
        const float index = 2.4f * std::exp(-t / 0.12f);
        const float v = std::sin(kTau * freq * t + index * std::sin(kTau * freq * 1.41f * t));
        out.samples[i] += gain * 0.8f * v * Decay(t, 0.003f, 0.55f);
      }
      break;
    }
    case Color::Rose: {  // a soft reed: odd harmonics, a little vibrato
      for (size_t i = from; i < out.samples.size(); ++i) {
        const float t = out.T(i - from);
        const float f = freq * (1.0f + 0.004f * std::sin(kTau * 5.5f * t));
        const float v =
            std::sin(kTau * f * t) + 0.3f * std::sin(3.0f * kTau * f * t) + 0.12f * std::sin(5.0f * kTau * f * t);
        out.samples[i] += gain * 0.7f * v * Decay(t, 0.02f, 0.3f);
      }
      break;
    }
    default: {  // pearl: a celesta, bright and short
      for (size_t i = from; i < out.samples.size(); ++i) {
        const float t = out.T(i - from);
        const float v = std::sin(kTau * freq * t) + 0.35f * std::sin(2.0f * kTau * freq * t) +
                        0.12f * std::sin(3.0f * kTau * freq * t) * std::exp(-t / 0.05f);
        out.samples[i] += gain * 0.7f * v * Decay(t, 0.001f, 0.35f);
      }
      break;
    }
  }
}

float BaseFreq(Color color) {
  switch (color) {
    case Color::Amber: return kC4;
    case Color::Pearl: return kC6;
    default: return kC5;
  }
}

void Sine(Buffer& out, float start, float length, float freq_from, float freq_to, float gain, float attack,
          float decay) {
  float phase = 0.0f;
  const size_t from = out.At(start), to = out.At(start + length);
  for (size_t i = from; i < to; ++i) {
    const float t = out.T(i - from);
    const float f = freq_from + (freq_to - freq_from) * std::min(1.0f, t / std::max(length, 1e-3f));
    phase += kTau * f / static_cast<float>(out.rate);
    out.samples[i] += gain * std::sin(phase) * Decay(t, attack, decay);
  }
}

void Normalize(std::vector<float>& samples, float peak) {
  float most = 0.0f;
  for (float s : samples) most = std::max(most, std::fabs(s));
  if (most <= 0.0f) return;
  for (float& s : samples) s *= peak / most;
  // A short fade at the end: no click when a sound is cut.
  const size_t fade = std::min<size_t>(samples.size(), 64);
  for (size_t i = 0; i < fade; ++i) samples[samples.size() - 1 - i] *= static_cast<float>(i) / static_cast<float>(fade);
}

Color ColorOf(Sound sound) {
  switch (sound) {
    case Sound::NoteBlue:
    case Sound::ChordBlue: return Color::Blue;
    case Sound::NoteAmber:
    case Sound::ChordAmber: return Color::Amber;
    case Sound::NoteGreen:
    case Sound::ChordGreen: return Color::Green;
    case Sound::NoteRose:
    case Sound::ChordRose: return Color::Rose;
    default: return Color::Pearl;
  }
}

}  // namespace

std::vector<float> Make(Sound sound, uint32_t rate) {
  switch (sound) {
    case Sound::NoteBlue:
    case Sound::NoteAmber:
    case Sound::NoteGreen:
    case Sound::NoteRose:
    case Sound::NotePearl: {
      const Color color = ColorOf(sound);
      Buffer out(rate, color == Color::Amber ? 0.9f : 0.75f);
      Instrument(color, BaseFreq(color), out, 0.0f, 1.0f);
      Normalize(out.samples, 0.8f);
      return out.samples;
    }
    case Sound::ChordBlue:
    case Sound::ChordAmber:
    case Sound::ChordGreen:
    case Sound::ChordRose:
    case Sound::ChordPearl: {  // a strummed major chord, an octave up for the top note
      const Color color = ColorOf(sound);
      Buffer out(rate, 1.6f);
      const float f = BaseFreq(color);
      const float ratios[] = {1.0f, 1.25f, 1.5f, 2.0f};
      for (int k = 0; k < 4; ++k) Instrument(color, f * ratios[k], out, 0.035f * static_cast<float>(k), 0.6f);
      Normalize(out.samples, 0.85f);
      return out.samples;
    }
    case Sound::Thud: {  // low, dull, no shine
      Buffer out(rate, 0.45f);
      Sine(out, 0.0f, 0.45f, 120.0f, 55.0f, 1.0f, 0.004f, 0.12f);
      Noise noise;
      float low = 0.0f;
      for (size_t i = 0; i < out.At(0.05f); ++i) {
        low += 0.08f * (noise.Next() - low);
        out.samples[i] += 0.8f * low * Decay(out.T(i), 0.002f, 0.015f);
      }
      Normalize(out.samples, 0.9f);
      return out.samples;
    }
    case Sound::Splash: {  // noise through a falling resonant filter
      Buffer out(rate, 0.42f);
      Noise noise;
      float low = 0.0f, band = 0.0f;
      for (size_t i = 0; i < out.samples.size(); ++i) {
        const float t = out.T(i);
        const float f = 1400.0f * std::exp(-t * 5.0f) + 250.0f;
        const float k = 2.0f * std::sin(std::numbers::pi_v<float> * f / static_cast<float>(rate));
        low += k * band;
        const float high = noise.Next() - low - 0.25f * band;
        band += k * high;
        out.samples[i] = band * Decay(t, 0.01f, 0.12f);
      }
      Normalize(out.samples, 0.7f);
      return out.samples;
    }
    case Sound::Chime: {  // the signature sparkle: two high partials, a shimmer
      Buffer out(rate, 0.8f);
      Sine(out, 0.0f, 0.8f, 2093.0f, 2093.0f, 1.0f, 0.002f, 0.28f);
      Sine(out, 0.03f, 0.77f, 3136.0f, 3136.0f, 0.6f, 0.002f, 0.2f);
      Sine(out, 0.06f, 0.7f, 4186.0f, 4186.0f, 0.3f, 0.002f, 0.12f);
      for (size_t i = 0; i < out.samples.size(); ++i) out.samples[i] *= 1.0f + 0.25f * std::sin(kTau * 9.0f * out.T(i));
      Normalize(out.samples, 0.55f);
      return out.samples;
    }
    case Sound::Grind: {  // a grinding stop, then a low organ chord
      Buffer out(rate, 2.4f);
      Noise noise;
      float phase = 0.0f, low = 0.0f;
      for (size_t i = 0; i < out.At(0.55f); ++i) {
        const float t = out.T(i);
        const float f = 70.0f - 40.0f * t;
        phase += f / static_cast<float>(rate);
        const float saw = 2.0f * (phase - std::floor(phase)) - 1.0f;
        low += 0.2f * (noise.Next() - low);
        out.samples[i] += (0.6f * saw + 0.5f * low) * Decay(t, 0.005f, 0.25f);
      }
      const float organ[] = {65.41f, 98.0f, 130.81f, 155.56f};  // C2 G2 C3 Eb3
      for (float f : organ) {
        for (size_t i = out.At(0.35f); i < out.samples.size(); ++i) {
          const float t = out.T(i - out.At(0.35f));
          const float env = std::min(1.0f, t / 0.25f) * std::exp(-std::max(0.0f, t - 1.2f) / 0.35f);
          const float v =
              std::sin(kTau * f * t) + 0.5f * std::sin(2.0f * kTau * f * t) + 0.25f * std::sin(4.0f * kTau * f * t);
          out.samples[i] += 0.3f * v * env;
        }
      }
      Normalize(out.samples, 0.85f);
      return out.samples;
    }
    case Sound::Fanfare: {  // a rising arpeggio on the marimba over a soft glass chord
      Buffer out(rate, 1.8f);
      const float notes[] = {kC5, kC5 * 1.25f, kC5 * 1.5f, kC6};
      for (int k = 0; k < 4; ++k) Instrument(Color::Blue, notes[k], out, 0.1f * static_cast<float>(k), 0.8f);
      for (float f : {kC5, kC5 * 1.25f, kC5 * 1.5f}) Instrument(Color::Green, f, out, 0.42f, 0.35f);
      Normalize(out.samples, 0.85f);
      return out.samples;
    }
    case Sound::Star: {
      Buffer out(rate, 0.7f);
      Sine(out, 0.0f, 0.7f, 1568.0f, 1568.0f, 1.0f, 0.002f, 0.3f);
      Sine(out, 0.0f, 0.7f, 2352.0f, 2352.0f, 0.5f, 0.002f, 0.2f);
      Normalize(out.samples, 0.6f);
      return out.samples;
    }
    case Sound::Pour: {
      Buffer out(rate, 0.12f);
      Sine(out, 0.0f, 0.12f, 280.0f, 520.0f, 1.0f, 0.004f, 0.04f);
      Normalize(out.samples, 0.35f);
      return out.samples;
    }
    case Sound::Click: {
      Buffer out(rate, 0.05f);
      Sine(out, 0.0f, 0.05f, 1800.0f, 1500.0f, 1.0f, 0.0005f, 0.008f);
      Noise noise;
      for (size_t i = 0; i < out.At(0.004f); ++i) out.samples[i] += 0.5f * noise.Next();
      Normalize(out.samples, 0.4f);
      return out.samples;
    }
    case Sound::Place: {
      Buffer out(rate, 0.14f);
      Sine(out, 0.0f, 0.14f, 220.0f, 150.0f, 1.0f, 0.001f, 0.035f);
      Sine(out, 0.0f, 0.05f, 1200.0f, 900.0f, 0.4f, 0.0005f, 0.01f);
      Normalize(out.samples, 0.6f);
      return out.samples;
    }
    case Sound::Lift: {
      Buffer out(rate, 0.06f);
      Sine(out, 0.0f, 0.06f, 700.0f, 1150.0f, 1.0f, 0.002f, 0.025f);
      Normalize(out.samples, 0.3f);
      return out.samples;
    }
    case Sound::Tap:
    case Sound::Count: {
      Buffer out(rate, 0.06f);
      Sine(out, 0.0f, 0.06f, 1250.0f, 1250.0f, 1.0f, 0.001f, 0.015f);
      Normalize(out.samples, 0.35f);
      return out.samples;
    }
  }
  return {};
}

Sound NoteFor(Color color) {
  switch (color) {
    case Color::Blue: return Sound::NoteBlue;
    case Color::Amber: return Sound::NoteAmber;
    case Color::Green: return Sound::NoteGreen;
    case Color::Rose: return Sound::NoteRose;
    default: return Sound::NotePearl;
  }
}

Sound ChordFor(Color color) {
  switch (color) {
    case Color::Blue: return Sound::ChordBlue;
    case Color::Amber: return Sound::ChordAmber;
    case Color::Green: return Sound::ChordGreen;
    case Color::Rose: return Sound::ChordRose;
    default: return Sound::ChordPearl;
  }
}

float ClimbPitch(int step) {
  // C major pentatonic, climbing (C D E G A, then up an octave).
  static constexpr float kScale[] = {1.0f, 1.125f, 1.25f, 1.5f, 1.6667f};
  step = std::max(step, 0);
  const int octave = std::min(step / 5, 2);
  return kScale[step % 5] * static_cast<float>(1 << octave);
}

}  // namespace threadlock::synth
