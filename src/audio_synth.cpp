#include "audio_synth.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <numbers>

#include "apothecary_look.h"
#include "barges.h"

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

// One partial, amp sin(2 pi freq t + phase) Decay(t, attack, time), added from sample `from`: a phasor turned
// by a fixed step each sample (no sin or exp per sample), stopped once it's below hearing. Every struck or
// rubbed sound (bars, bells, glass) is a sum of these: the long ones ring 20-60 partials for seconds.
void Partial(Buffer& out, size_t from, float freq, float amp, float attack, float time, float phase = 0.0f) {
  const double step = static_cast<double>(kTau) * freq / static_cast<double>(out.rate);
  const double c = std::cos(step), s = std::sin(step);
  const double fall = std::exp(-1.0 / (static_cast<double>(time) * out.rate));
  const size_t rise = std::max<size_t>(1, static_cast<size_t>(attack * static_cast<float>(out.rate)));
  double re = std::cos(phase), im = std::sin(phase), env = 1.0;  // (re, im) = e^(i (step n + phase))
  for (size_t i = from, n = 0; i < out.samples.size(); ++i, ++n) {
    double level = static_cast<double>(n) / static_cast<double>(rise);
    if (n >= rise) {
      level = env;
      env *= fall;
      if (env < 1e-5) break;
    }
    out.samples[i] += static_cast<float>(amp * level * im);
    const double next = re * c - im * s;
    im = re * s + im * c;
    re = next;
  }
}

// One instrument note at `freq`, added into `out` from `start` seconds, scaled by `gain`.
void Instrument(Color color, float freq, Buffer& out, float start, float gain) {
  const size_t from = out.At(start);
  switch (color) {
    case Color::Blue: {  // marimba: a struck bar's inharmonic partials, the high ones die first
      const float ratio[] = {1.0f, 3.93f, 9.0f}, amp[] = {1.0f, 0.32f, 0.08f}, time[] = {0.38f, 0.09f, 0.03f};
      for (int k = 0; k < 3; ++k) Partial(out, from, freq * ratio[k], gain * amp[k], 0.002f, time[k]);
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

// ---- The Lantern set: glass and bells ----

// A glass harmonica: a rubbed glass bowl, almost a pure sine with weak 2nd and 3rd partials, a slow
// attack, and two partials 0.7 Hz apart (the bowl is never perfectly round: a slow beating).
void Harmonica(Buffer& out, float freq, float start, float gain, float attack = 0.07f, float decay = 0.32f) {
  const size_t from = out.At(start);
  const float g = gain * 0.6f;
  Partial(out, from, freq, g, attack, decay);
  Partial(out, from, freq + 0.7f, g * 0.6f, attack, decay, 1.3f);
  Partial(out, from, 2.0f * freq, g * 0.12f, attack, decay);
  Partial(out, from, 3.0f * freq, g * 0.05f, attack, decay);
}

// A celesta: a hammer on a small steel bar over a resonator: bright, quick, the 3.9 partial gone
// almost at once.
void Celesta(Buffer& out, float freq, float start, float gain) {
  const float ratio[] = {1.0f, 2.0f, 3.9f}, amp[] = {1.0f, 0.3f, 0.18f}, time[] = {0.2f, 0.11f, 0.03f};
  const size_t from = out.At(start);
  for (int k = 0; k < 3; ++k) Partial(out, from, freq * ratio[k], gain * amp[k], 0.001f, time[k]);
}

// A tubular chime: a free-free bar's partials 1 : 2.756 : 5.404 : 8.933 (the high ones die first),
// struck with a short click.
void TubularChime(Buffer& out, float freq, float start, float gain) {
  const float ratio[] = {1.0f, 2.756f, 5.404f, 8.933f}, amp[] = {1.0f, 0.45f, 0.25f, 0.12f};
  const float time[] = {0.45f, 0.3f, 0.14f, 0.06f};
  const size_t from = out.At(start);
  for (int k = 0; k < 4; ++k) Partial(out, from, freq * ratio[k], gain * amp[k], 0.0015f, time[k]);
  Noise noise;
  float last = 0.0f;
  for (size_t i = from; i < out.At(start + 0.004f); ++i) {  // the strike: a high-passed tick
    const float n = noise.Next();
    out.samples[i] += gain * 0.35f * (n - last) * Decay(out.T(i - from), 0.0002f, 0.001f);
    last = n;
  }
}

// A small glass bell (a window pane lighting): the fundamental, a soft octave and a glassy 2.76.
void PaneBell(Buffer& out, float freq, float start, float gain) {
  const float ratio[] = {1.0f, 2.0f, 2.76f}, amp[] = {1.0f, 0.2f, 0.3f}, time[] = {0.14f, 0.08f, 0.05f};
  const size_t from = out.At(start);
  for (int k = 0; k < 3; ++k) Partial(out, from, freq * ratio[k], gain * amp[k], 0.001f, time[k]);
}

// A state-variable filter (Chamberlin): band-passed and low-passed noise for smoke, liquid and air.
struct Svf {
  float low = 0.0f, band = 0.0f;
  float Band(float in, float freq, float damping, uint32_t rate) { return BandK(in, K(freq, rate), damping); }
  // A fixed band: the coefficient worked out once (K), not a sine per sample.
  float BandK(float in, float k, float damping) {
    low += k * band;
    band += k * (in - low - damping * band);
    return band;
  }
  static float K(float freq, uint32_t rate) {
    return 2.0f * std::sin(std::numbers::pi_v<float> * freq / static_cast<float>(rate));
  }
};

// The heart's 12 chimes on a pentatonic climb, at the art's own timing.
void HeartChimes(Buffer& out, float start, float base, float gain) {
  for (int j = 0; j < 12; ++j) PaneBell(out, base * ClimbPitch(j), start + HeartOnset(j), gain);
}

void Normalize(std::vector<float>& samples, float peak);

// ---- The Shop set: wood and brass ----

// A rosewood marimba: a tuned bar's partials 1 : 4 : 10 (the high ones die first), a resonator tube
// under it holding the fundamental a little longer, a soft mallet thump.
void Marimba(Buffer& out, float freq, float start, float gain) {
  const float ratio[] = {1.0f, 4.0f, 10.0f}, amp[] = {1.0f, 0.22f, 0.05f}, time[] = {0.55f, 0.1f, 0.025f};
  const size_t from = out.At(start);
  Partial(out, from, freq, gain * 0.35f, 0.012f, 0.8f);  // the resonator
  for (int k = 0; k < 3; ++k) Partial(out, from, freq * ratio[k], gain * amp[k], 0.0015f, time[k]);
  Noise noise;
  float low = 0.0f;
  for (size_t i = from; i < out.At(start + 0.012f); ++i) {  // the mallet: a short low-passed knock
    low += 0.15f * (noise.Next() - low);
    out.samples[i] += gain * 0.5f * low * Decay(out.T(i - from), 0.0005f, 0.003f);
  }
}

// A pizzicato string: a finger's pluck (low-passed noise, softer than a pick) in a Karplus-Strong loop
// that loses its top quickly, through the instrument's body (two wooden resonances).
void Pizzicato(Buffer& out, float freq, float start, float gain) {
  Noise noise;
  const size_t period = std::max<size_t>(2, static_cast<size_t>(static_cast<float>(out.rate) / freq));
  std::vector<float> line(period);
  float soft = 0.0f;
  for (float& s : line) s = soft += 0.5f * (noise.Next() - soft);
  size_t pos = 0;
  float last = 0.0f;
  Svf body_low, body_high;
  const size_t from = out.At(start);
  for (size_t i = from; i < out.samples.size(); ++i) {
    const float current = line[pos];
    line[pos] = 0.5f * (current + last) * 0.992f;
    last = current;
    pos = (pos + 1) % period;
    const float body =
        0.6f * body_low.Band(current, 280.0f, 0.9f, out.rate) + 0.35f * body_high.Band(current, 950.0f, 1.1f, out.rate);
    const float env = Decay(out.T(i - from), 0.001f, 0.3f);
    if (i > from + 100 && env < 1e-5f) break;  // gone
    out.samples[i] += gain * (0.7f * current + body) * env;
  }
}

// The shop's door bell: a small brass bell (a minor-third bell's partials, the hum below), the clapper
// striking twice as it swings.
void ShopBell(Buffer& out, float freq, float start, float gain) {
  const float ratio[] = {0.5f, 1.0f, 1.19f, 1.5f, 2.0f, 2.66f}, amp[] = {0.25f, 1.0f, 0.5f, 0.3f, 0.35f, 0.15f};
  const float time[] = {0.9f, 0.6f, 0.45f, 0.3f, 0.2f, 0.1f};
  for (int strike = 0; strike < 2; ++strike) {
    const size_t from = out.At(start + 0.13f * static_cast<float>(strike));
    const float g = gain * (strike == 0 ? 1.0f : 0.45f);
    for (int k = 0; k < 6; ++k) Partial(out, from, freq * ratio[k], g * amp[k], 0.001f, time[k]);
  }
}

// A knock on wood: a short low tone and a band of noise.
void Knock(Buffer& out, float start, float freq, float gain) {
  Sine(out, start, 0.12f, freq, freq * 0.85f, gain, 0.001f, 0.035f);
  Noise noise;
  Svf filter;
  const size_t from = out.At(start);
  for (size_t i = from; i < out.At(start + 0.04f); ++i) {
    out.samples[i] +=
        gain * 0.8f * filter.Band(noise.Next(), freq * 5.0f, 0.8f, out.rate) * Decay(out.T(i - from), 0.0005f, 0.008f);
  }
}

// The PAID stamp: a rubber stamp thumps onto paper on the counter (a low thud, a papery slap).
void Stamp(Buffer& out, float start, float gain) {
  Sine(out, start, 0.25f, 150.0f, 65.0f, gain, 0.001f, 0.06f);
  Noise noise;
  float low = 0.0f;
  const size_t from = out.At(start);
  for (size_t i = from; i < out.At(start + 0.06f); ++i) {
    low += 0.3f * (noise.Next() - low);
    out.samples[i] += gain * 0.7f * low * Decay(out.T(i - from), 0.0005f, 0.012f);
  }
}

// A cork pushed into a bottle's mouth: a short squeaky pop.
void Cork(Buffer& out, float start, float gain) {
  Sine(out, start, 0.06f, 700.0f, 330.0f, gain, 0.0008f, 0.018f);
  Noise noise;
  const size_t from = out.At(start);
  for (size_t i = from; i < out.At(start + 0.003f); ++i) out.samples[i] += gain * 0.3f * noise.Next();
}

// A drop falling onto a hard surface (a bubble whose pitch rises), at `freq`.
void Plink(Buffer& out, float start, float freq, float gain) {
  float phase = 0.0f;
  const size_t from = out.At(start), to = out.At(start + 0.1f);
  for (size_t i = from; i < to; ++i) {
    const float t = out.T(i - from);
    phase += kTau * freq * (1.0f + 9.0f * t) / static_cast<float>(out.rate);
    out.samples[i] += gain * std::sin(phase) * Decay(t, 0.0008f, 0.02f);
  }
}

// An order filled, timed like its moment (apothecary_look.h): the cork, the colour's chord rolled
// under it, the shop bell, the PAID stamp.
std::vector<float> Paid(Color color, uint32_t rate) {
  Buffer out(rate, apothecary::kPaidStamp + 1.1f);
  Cork(out, apothecary::kPaidPop, 0.6f);
  const float chord[] = {kC4, kC4 * 1.25f, kC4 * 1.5f, kC5};
  for (int k = 0; k < 4; ++k) {
    const float at = apothecary::kPaidPop + 0.06f + 0.045f * static_cast<float>(k);
    if (color == Color::Amber) {
      Pizzicato(out, chord[k], at, 0.5f);
    } else {
      Marimba(out, chord[k], at, 0.45f);
    }
  }
  ShopBell(out, color == Color::Amber ? kC6 * 1.5f : kC6, apothecary::kPaidBell, 0.6f);
  Stamp(out, apothecary::kPaidStamp, 0.9f);
  Normalize(out.samples, 0.85f);
  return out.samples;
}

// A grinding stop (a saw running down, rough noise), then a low organ chord (C minor): THREADLOCKED in
// every world.
void GrindStop(Buffer& out) {
  Noise noise;
  float phase = 0.0f, low = 0.0f;
  for (size_t i = 0; i < out.At(0.55f); ++i) {
    const float t = out.T(i);
    const float f = 70.0f - 40.0f * t;
    phase += f / static_cast<float>(out.rate);
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
}

// ---- The Canal set: a late-night jazz trio on the water ----

constexpr float kC3 = 130.81f;
// C6/9 from its root: C E G A D. It is the pentatonic itself, so it sits under anything the machine plays.
constexpr float kSixNine[] = {1.0f, 1.25f, 1.5f, 1.6667f, 2.25f};

float Ease(float x) {
  x = std::clamp(x, 0.0f, 1.0f);
  return x * x * (3.0f - 2.0f * x);
}

// A vibraphone: an aluminium bar tuned 1 : 4 : 10 that rings long, a soft yarn mallet, and the motor's
// tremolo (discs turning in the resonators, ~5.5 Hz). Amplitude modulation is the note plus two sidebands
// at f +- 5.5 Hz: three partials instead of a multiply per sample.
void Vibes(Buffer& out, float freq, float start, float gain, float ring = 1.0f) {
  constexpr float kMotor = 5.5f, kDepth = 0.35f;
  const size_t from = out.At(start);
  Partial(out, from, freq, gain, 0.002f, ring);
  Partial(out, from, freq + kMotor, gain * 0.5f * kDepth, 0.002f, ring);
  Partial(out, from, freq - kMotor, gain * 0.5f * kDepth, 0.002f, ring);
  Partial(out, from, freq * 4.0f, gain * 0.18f, 0.0015f, 0.12f);
  Partial(out, from, freq * 10.0f, gain * 0.05f, 0.001f, 0.03f);
}

// A plucked upright bass: Karplus-Strong like the pizzicato, but a fuller pluck that keeps more of its top
// (a phone's speaker can't play C3's fundamental: the ear finds it from the harmonics), through the big
// body's two resonances.
void UprightBass(Buffer& out, float freq, float start, float gain, float ring = 0.6f) {
  Noise noise;
  const size_t period = std::max<size_t>(2, static_cast<size_t>(static_cast<float>(out.rate) / freq));
  std::vector<float> line(period);
  float soft = 0.0f, mean = 0.0f;
  for (float& s : line) mean += s = soft += 0.35f * (noise.Next() - soft);
  mean /= static_cast<float>(period);
  for (float& s : line) s -= mean;  // no offset left ringing in the loop
  size_t pos = 0;
  float last = 0.0f;
  Svf body_low, body_mid;
  const float k_low = Svf::K(110.0f, out.rate), k_mid = Svf::K(600.0f, out.rate);
  // The envelope: a 2 ms rise, then a fixed factor a sample (no exp per sample), stopped below hearing.
  const size_t from = out.At(start), rise = out.At(0.002f);
  const float fall = std::exp(-1.0f / (ring * static_cast<float>(out.rate)));
  float env = 1.0f;
  for (size_t i = from, n = 0; i < out.samples.size(); ++i, ++n) {
    const float current = line[pos];
    line[pos] = 0.5f * (current + last) * 0.997f;
    last = current;
    if (++pos == period) pos = 0;
    const float body = 0.5f * body_low.BandK(current, k_low, 0.8f) + 0.4f * body_mid.BandK(current, k_mid, 1.0f);
    float level = static_cast<float>(n) / static_cast<float>(rise);
    if (n >= rise) {
      level = env;
      env *= fall;
      if (env < 1e-4f) break;  // gone
    }
    out.samples[i] += gain * (0.8f * current + body) * level;
  }
}

// A Rhodes: a hammer strikes a tine beside a pickup. The tone bar holds the fundamental with a little of
// the 2nd and 3rd; the tine's own high mode (~7.1x) is the bell, gone in a few tens of milliseconds.
void Rhodes(Buffer& out, float freq, float start, float gain, float ring = 0.9f) {
  const size_t from = out.At(start);
  Partial(out, from, freq, gain, 0.002f, ring);
  Partial(out, from, freq * 2.0f, gain * 0.22f, 0.002f, 0.35f * ring);
  Partial(out, from, freq * 3.0f, gain * 0.08f, 0.001f, 0.15f);
  Partial(out, from, freq * 7.1f, gain * 0.25f, 0.0005f, 0.04f);
}

// A boat's horn: a reed-like tone (harmonics 1..6 falling off), a scoop up into the note, held for `length`,
// let go over 0.25 s; `sag` is where the pitch has drooped to by the end (1 = steady). The fundamental is a
// phasor whose turn per sample is updated every 32 samples (the pitch moves slowly); the harmonics are its powers.
void Horn(Buffer& out, float freq, float start, float length, float gain, float sag = 1.0f) {
  constexpr float kAmp[] = {1.0f, 0.55f, 0.45f, 0.25f, 0.18f, 0.08f};
  constexpr float kRelease = 0.25f;
  const size_t from = out.At(start), to = out.At(start + length + kRelease);
  double re = 1.0, im = 0.0, c = 1.0, s = 0.0;
  for (size_t i = from, n = 0; i < to; ++i, ++n) {
    const float t = out.T(i - from);
    if (n % 32 == 0) {
      const float f = freq * (1.0f - 0.03f * std::exp(-t / 0.06f)) * (1.0f + (sag - 1.0f) * std::min(1.0f, t / length));
      const double step = static_cast<double>(kTau) * f / static_cast<double>(out.rate);
      c = std::cos(step);
      s = std::sin(step);
      const double size = std::sqrt(re * re + im * im);  // keep the phasor on the unit circle
      re /= size;
      im /= size;
    }
    double hr = re, hi = im, v = 0.0;
    for (float amp : kAmp) {
      v += amp * hi;
      const double next = hr * re - hi * im;
      hi = hr * im + hi * re;
      hr = next;
    }
    const double next = re * c - im * s;
    im = re * s + im * c;
    re = next;
    const float tail = t < length ? 1.0f : 1.0f - (t - length) / kRelease;
    const float env = Ease(t / 0.08f) * tail * tail;
    out.samples[i] += gain * 0.4f * static_cast<float>(v) * env;
  }
}

// A wire brush on a snare: a swish of high noise (the wires), the drum's head barely touched under it.
void Brush(Buffer& out, float start, float gain, float swish = 0.06f) {
  Noise noise;
  Svf wires;
  const float k = Svf::K(4200.0f, out.rate);
  const size_t from = out.At(start);
  for (size_t i = from; i < out.At(start + 6.0f * swish); ++i) {
    out.samples[i] += gain * wires.BandK(noise.Next(), k, 0.7f) * Decay(out.T(i - from), 0.004f, swish);
  }
  Sine(out, start, 0.08f, 190.0f, 170.0f, gain * 0.25f, 0.002f, 0.025f);
}

// A brushed ride cymbal: a wash of very high noise swelling from `start` to `peak` and gone by `end` (times in
// the buffer), with the plate's faint inharmonic ring.
void Cymbal(Buffer& out, float start, float peak, float end, float gain) {
  Noise noise;
  Svf a, b;
  const float ka = Svf::K(6500.0f, out.rate), kb = Svf::K(9500.0f, out.rate);
  for (size_t i = out.At(start); i < out.At(end); ++i) {
    const float t = out.T(i);
    const float env = t < peak ? Ease((t - start) / (peak - start)) : 1.0f - Ease((t - peak) / (end - peak));
    const float n = noise.Next();
    out.samples[i] += gain * env * (0.6f * a.BandK(n, ka, 0.5f) + 0.4f * b.BandK(n, kb, 0.5f));
  }
  const size_t from = out.At(start);
  for (float r : {1.0f, 1.47f, 2.09f, 2.56f}) {
    Partial(out, from, 3100.0f * r, gain * 0.03f / r, peak - start, 0.3f * (end - peak) / r);
  }
}

// A harbour served, timed like its light (barges.h): the harbour's horn as its lamps come on and its glow
// swells (over kWarmLength), the colour's 6/9 rolled under it, a brushed cymbal swelling with the district's
// light and gone when the light has run out (kDistrictLength).
std::vector<float> Harbour(Color color, uint32_t rate) {
  Buffer out(rate, barges::kDistrictLength + 0.5f);
  const float horn = color == Color::Amber ? 293.66f : color == Color::Green ? 220.0f : 196.0f;  // G3, D4, A3
  Horn(out, horn, 0.0f, barges::kWarmLength - 0.25f, 0.55f);
  for (int k = 0; k < 5; ++k) {
    const float at = 0.12f + 0.05f * static_cast<float>(k);
    if (color == Color::Amber) {
      UprightBass(out, kC3 * kSixNine[k], at, 0.4f, 0.5f);
    } else if (color == Color::Green) {
      Rhodes(out, kC4 * kSixNine[k], at, 0.3f, 0.6f);
    } else {
      Vibes(out, kC4 * kSixNine[k], at, 0.3f, 0.6f);
    }
  }
  Cymbal(out, 0.0f, 0.45f * barges::kDistrictLength, barges::kDistrictLength, 0.35f);
  Normalize(out.samples, 0.85f);
  return out.samples;
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
    case Sound::Grind: {
      Buffer out(rate, 2.4f);
      GrindStop(out);
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
    case Sound::HarmonicaNote: {
      Buffer out(rate, 1.4f);
      Harmonica(out, kC4, 0.0f, 1.0f);
      Normalize(out.samples, 0.8f);
      return out.samples;
    }
    case Sound::CelestaNote: {
      Buffer out(rate, 0.9f);
      Celesta(out, kC5, 0.0f, 1.0f);
      Normalize(out.samples, 0.8f);
      return out.samples;
    }
    case Sound::LanternBlue: {  // the chimes, a harmonica chord blooming under them
      Buffer out(rate, 2.4f);
      TubularChime(out, kC5, 0.0f, 1.0f);
      const float chord[] = {kC4, kC4 * 1.25f, kC4 * 1.5f};
      for (int k = 0; k < 3; ++k) Harmonica(out, chord[k], 0.04f * static_cast<float>(k), 0.45f, 0.18f, 0.45f);
      Normalize(out.samples, 0.85f);
      return out.samples;
    }
    case Sound::LanternAmber: {  // the chimes a fifth up, a quick celesta arpeggio
      Buffer out(rate, 2.2f);
      TubularChime(out, kC5 * 1.5f, 0.0f, 1.0f);
      const float chord[] = {kC5, kC5 * 1.25f, kC5 * 1.5f, kC6};
      for (int k = 0; k < 4; ++k) Celesta(out, chord[k], 0.05f + 0.06f * static_cast<float>(k), 0.35f);
      Normalize(out.samples, 0.85f);
      return out.samples;
    }
    case Sound::PaneChime: {
      Buffer out(rate, 0.7f);
      PaneBell(out, kC5, 0.0f, 1.0f);
      Normalize(out.samples, 0.6f);
      return out.samples;
    }
    case Sound::Heart: {  // a harmonica chord (Cadd9) blooming, the heart's 12 chimes over it
      Buffer out(rate, 2.4f);
      for (float f : {kC4, kC4 * 1.25f, kC4 * 1.5f, kC5 * 1.125f}) Harmonica(out, f, 0.0f, 0.35f, 0.35f, 0.5f);
      HeartChimes(out, 0.05f, kC5, 0.4f);
      Normalize(out.samples, 0.85f);
      return out.samples;
    }
    case Sound::RoseWindow: {  // the finale: a deep bell, a lower and longer bloom, the chimes, a shimmer
      Buffer out(rate, 4.2f);
      TubularChime(out, kC4, 0.0f, 0.7f);
      for (float f : {kC4 * 0.5f, kC4 * 0.75f, kC4, kC4 * 1.25f, kC5 * 1.125f})
        Harmonica(out, f, 0.1f, 0.3f, 0.8f, 0.75f);
      HeartChimes(out, 0.3f, kC5, 0.35f);
      const float shimmer[] = {kC6 * 2.0f, kC6 * 2.25f, kC6 * 2.5f, kC6 * 3.0f};
      for (int k = 0; k < 4; ++k) {
        const size_t from = out.At(2.5f + 0.07f * static_cast<float>(k));
        for (size_t i = from; i < out.samples.size(); ++i) {
          const float t = out.T(i - from);
          const float tremolo = 1.0f + 0.3f * std::sin(kTau * 7.0f * t + static_cast<float>(k));
          out.samples[i] += 0.07f * tremolo * std::sin(kTau * shimmer[k] * t) * Decay(t, 0.25f, 0.3f);
        }
      }
      Normalize(out.samples, 0.85f);
      return out.samples;
    }
    case Sound::Smoke: {  // a muffled knock on glass, then a low hiss that swells and goes
      Buffer out(rate, 0.8f);
      Sine(out, 0.0f, 0.3f, 210.0f, 160.0f, 1.0f, 0.002f, 0.05f);
      Sine(out, 0.0f, 0.15f, 480.0f, 400.0f, 0.3f, 0.001f, 0.025f);
      Noise noise;
      Svf filter;
      for (size_t i = 0; i < out.samples.size(); ++i) {
        const float t = out.T(i);
        const float env = Decay(t, 0.15f, 0.14f);
        out.samples[i] += 0.9f * env * filter.Band(noise.Next(), 500.0f - 250.0f * t, 0.7f, rate);
      }
      Normalize(out.samples, 0.8f);
      return out.samples;
    }
    case Sound::Drip: {  // a bubble: a sine whose pitch rises fast, f(t) = f0 (1 + s t), gone at once
      Buffer out(rate, 0.12f);
      float phase = 0.0f;
      for (size_t i = 0; i < out.samples.size(); ++i) {
        const float t = out.T(i);
        phase += kTau * 1100.0f * (1.0f + 11.0f * t) / static_cast<float>(rate);
        out.samples[i] = std::sin(phase) * Decay(t, 0.0008f, 0.025f);
      }
      Normalize(out.samples, 0.6f);
      return out.samples;
    }
    case Sound::CruciblePour: {  // a low soft glass tone, a little liquid
      Buffer out(rate, 0.3f);
      Harmonica(out, 196.0f, 0.0f, 1.0f, 0.012f, 0.06f);
      Noise noise;
      Svf filter;
      for (size_t i = 0; i < out.samples.size(); ++i) {
        const float t = out.T(i);
        out.samples[i] += 0.5f * Decay(t, 0.01f, 0.04f) * filter.Band(noise.Next(), 900.0f + 600.0f * t, 0.3f, rate);
      }
      Normalize(out.samples, 0.35f);
      return out.samples;
    }
    case Sound::Ascent: {  // a quick climb of small bells
      Buffer out(rate, 0.9f);
      for (int k = 0; k < 8; ++k) PaneBell(out, kC5 * ClimbPitch(k), 0.07f * static_cast<float>(k), 0.5f);
      Normalize(out.samples, 0.45f);
      return out.samples;
    }
    case Sound::MarimbaNote: {
      Buffer out(rate, 2.0f);
      Marimba(out, kC4, 0.0f, 1.0f);
      Normalize(out.samples, 0.8f);
      return out.samples;
    }
    case Sound::PizzNote: {
      Buffer out(rate, 1.0f);
      Pizzicato(out, kC4, 0.0f, 1.0f);
      Normalize(out.samples, 0.8f);
      return out.samples;
    }
    case Sound::PaidBlue: return Paid(Color::Blue, rate);
    case Sound::PaidAmber: return Paid(Color::Amber, rate);
    case Sound::Sludge: {  // a thick bubble bursting low (its pitch wobbling down), then a knock on wood
      Buffer out(rate, 0.7f);
      float phase = 0.0f;
      for (size_t i = 0; i < out.At(0.35f); ++i) {
        const float t = out.T(i);
        const float f = (95.0f - 45.0f * t / 0.35f) * (1.0f + 0.12f * std::sin(kTau * 18.0f * t));
        phase += kTau * f / static_cast<float>(rate);
        out.samples[i] += std::sin(phase) * Decay(t, 0.01f, 0.09f);
      }
      Knock(out, 0.07f, 170.0f, 0.7f);
      Noise noise;
      Svf filter;
      for (size_t i = 0; i < out.samples.size(); ++i) {  // the wet part, low and slow
        const float t = out.T(i);
        out.samples[i] += 0.6f * Decay(t, 0.02f, 0.12f) * filter.Band(noise.Next(), 320.0f - 150.0f * t, 0.6f, rate);
      }
      Normalize(out.samples, 0.85f);
      return out.samples;
    }
    case Sound::JarTap: {  // a short glug from the jar's tap: a low bubble and a little water
      Buffer out(rate, 0.18f);
      Plink(out, 0.0f, 260.0f, 1.0f);
      Noise noise;
      Svf filter;
      for (size_t i = 0; i < out.samples.size(); ++i) {
        const float t = out.T(i);
        out.samples[i] += 0.4f * Decay(t, 0.008f, 0.03f) * filter.Band(noise.Next(), 600.0f + 900.0f * t, 0.4f, rate);
      }
      Normalize(out.samples, 0.35f);
      return out.samples;
    }
    case Sound::SpillWood: {  // a splash on the counter, then drips off its edge onto the floor
      Buffer out(rate, 1.2f);
      Noise noise;
      Svf filter;
      for (size_t i = 0; i < out.At(0.45f); ++i) {
        const float t = out.T(i);
        out.samples[i] +=
            Decay(t, 0.012f, 0.1f) * filter.Band(noise.Next(), 1200.0f * std::exp(-t * 6.0f) + 220.0f, 0.3f, rate);
      }
      Knock(out, 0.0f, 140.0f, 0.4f);
      const float drips[] = {0.42f, 0.66f, 0.95f};
      for (int k = 0; k < 3; ++k) Plink(out, drips[k], 900.0f - 120.0f * static_cast<float>(k), 0.35f);
      Normalize(out.samples, 0.75f);
      return out.samples;
    }
    case Sound::ShortDull: {  // a dull knock, then two pizzicato notes falling (E4, C4): not enough
      Buffer out(rate, 1.1f);
      Knock(out, 0.0f, 120.0f, 0.9f);
      Pizzicato(out, kC4 * 1.25f, 0.16f, 0.6f);
      Pizzicato(out, kC4, 0.38f, 0.6f);
      Normalize(out.samples, 0.75f);
      return out.samples;
    }
    case Sound::RackClack: {  // a turntable on its pin clacking a notch round: two quick wooden knocks
      Buffer out(rate, 0.18f);
      Knock(out, 0.0f, 420.0f, 1.0f);
      Knock(out, 0.018f, 560.0f, 0.5f);
      Normalize(out.samples, 0.4f);
      return out.samples;
    }
    case Sound::Till:  // the last stamp comes down first (the moment's 1.3 s), then the till rings
    case Sound::MarketDay: {
      const bool grand = sound == Sound::MarketDay;
      const float start = apothecary::kPaidStamp + 0.25f;
      Buffer out(rate, start + (grand ? 4.2f : 2.4f));
      const int run = grand ? 10 : 6;  // up the pentatonic from C4
      for (int k = 0; k < run; ++k) Marimba(out, kC4 * ClimbPitch(k), start + 0.07f * static_cast<float>(k), 0.45f);
      if (grand) {
        for (int k = 0; k < 8; ++k)  // the strings answer from below
          Pizzicato(out, kC4 * 0.5f * ClimbPitch(k), start + 0.35f + 0.07f * static_cast<float>(k), 0.4f);
      }
      const float ring = start + 0.07f * static_cast<float>(run) + 0.08f;
      ShopBell(out, kC6, ring, 0.4f);
      ShopBell(out, kC6 * 1.25f, ring + 0.05f, 0.25f);
      // The coins: small bright pings, each a steel disc (1 : 2.3 : 3.9), dropping into the drawer.
      Noise noise;
      for (int c = 0; c < (grand ? 9 : 5); ++c) {
        const float at = ring + 0.12f + 0.08f * static_cast<float>(c) + 0.03f * noise.Next();
        const float f = 2600.0f + 500.0f * noise.Next();
        for (float r : {1.0f, 2.3f, 3.9f}) Sine(out, at, 0.25f, f * r, f * r, 0.12f / r, 0.0005f, 0.05f);
      }
      if (grand) {  // the chord on both, the bells pealing over it
        const float chord_at = ring + 0.7f;
        for (float f : {kC4 * 0.5f, kC4, kC4 * 1.25f, kC4 * 1.5f, kC5}) {
          Marimba(out, f, chord_at, 0.4f);
          Pizzicato(out, f, chord_at + 0.02f, 0.35f);
        }
        const float peal[] = {kC6 * 1.5f, kC6 * 1.25f, kC6, kC6 * 1.5f, kC6 * 2.0f};
        for (int k = 0; k < 5; ++k) ShopBell(out, peal[k], chord_at + 0.25f * static_cast<float>(k), 0.25f);
      }
      Normalize(out.samples, 0.85f);
      return out.samples;
    }
    case Sound::CorkPop: {  // the moment's cork alone, with a hollow knock of the glass under it
      Buffer out(rate, 0.35f);
      Cork(out, 0.0f, 1.0f);
      Knock(out, 0.01f, 520.0f, 0.25f);
      Normalize(out.samples, 0.6f);
      return out.samples;
    }
    case Sound::ShopBell: {  // the door bell alone, left to ring out
      Buffer out(rate, 3.4f);
      ShopBell(out, kC6, 0.0f, 1.0f);
      Normalize(out.samples, 0.7f);
      return out.samples;
    }
    case Sound::VibesNote: {
      Buffer out(rate, 2.8f);
      Vibes(out, kC4, 0.0f, 1.0f);
      Normalize(out.samples, 0.8f);
      return out.samples;
    }
    case Sound::BassNote: {
      Buffer out(rate, 1.6f);
      UprightBass(out, kC3, 0.0f, 1.0f, 0.4f);
      Normalize(out.samples, 0.8f);
      return out.samples;
    }
    case Sound::RhodesNote: {
      Buffer out(rate, 2.2f);
      Rhodes(out, kC4, 0.0f, 1.0f);
      Normalize(out.samples, 0.8f);
      return out.samples;
    }
    case Sound::HarbourBlue: return Harbour(Color::Blue, rate);
    case Sound::HarbourAmber: return Harbour(Color::Amber, rate);
    case Sound::HarbourGreen: return Harbour(Color::Green, rate);
    case Sound::LockBrush: {
      Buffer out(rate, 0.4f);
      Brush(out, 0.0f, 1.0f);
      Normalize(out.samples, 0.35f);
      return out.samples;
    }
    case Sound::MillClunk: {  // a heavy wooden clunk, then the iron pawl dropping into the next tooth
      Buffer out(rate, 0.35f);
      Knock(out, 0.0f, 210.0f, 1.0f);
      Knock(out, 0.0f, 140.0f, 0.5f);
      Partial(out, out.At(0.045f), 2400.0f, 0.25f, 0.0003f, 0.012f);
      Partial(out, out.At(0.045f), 3700.0f, 0.15f, 0.0003f, 0.008f);
      Normalize(out.samples, 0.5f);
      return out.samples;
    }
    case Sound::GateClank: {  // two iron leaves meeting: a struck plate's inharmonic partials, twice, on a thud
      Buffer out(rate, 0.6f);
      const float ratio[] = {1.0f, 2.32f, 3.86f, 5.4f}, amp[] = {1.0f, 0.6f, 0.35f, 0.2f};
      const float time[] = {0.16f, 0.1f, 0.06f, 0.035f};
      for (int leaf = 0; leaf < 2; ++leaf) {
        const size_t from = out.At(0.05f * static_cast<float>(leaf));
        const float f = leaf == 0 ? 340.0f : 362.0f, g = leaf == 0 ? 1.0f : 0.6f;
        for (int k = 0; k < 4; ++k) Partial(out, from, f * ratio[k], g * amp[k], 0.0008f, time[k]);
      }
      Knock(out, 0.0f, 150.0f, 0.6f);
      Normalize(out.samples, 0.5f);
      return out.samples;
    }
    case Sound::Murk: {  // two hulls bump (hollow knocks), then the water churns murky and settles (the stain's 1.4 s)
      Buffer out(rate, 1.6f);
      Knock(out, 0.0f, 95.0f, 1.0f);
      Knock(out, 0.11f, 80.0f, 0.7f);
      Noise noise;
      Svf filter;
      for (size_t i = 0; i < out.At(1.5f); ++i) {
        const float t = out.T(i);
        const float churn =
            Decay(t, 0.12f, 0.45f) * (1.0f + 0.5f * std::sin(kTau * 3.2f * t)) * (1.0f - Ease((t - 1.1f) / 0.4f));
        out.samples[i] += 0.7f * churn * filter.Band(noise.Next(), 260.0f - 120.0f * t / 1.5f, 0.5f, rate);
      }
      Normalize(out.samples, 0.85f);
      return out.samples;
    }
    case Sound::Wash: {  // the overflowing harbour's wake slaps the quay: a low splash, then smaller slaps
      Buffer out(rate, 1.4f);
      Noise noise;
      Svf filter;
      const float at[] = {0.0f, 0.32f, 0.6f, 0.85f}, gain[] = {1.0f, 0.55f, 0.35f, 0.2f};
      for (int k = 0; k < 4; ++k) {
        const size_t from = out.At(at[k]);
        for (size_t i = from; i < out.At(at[k] + 0.4f); ++i) {
          const float t = out.T(i - from);
          out.samples[i] += gain[k] * Decay(t, 0.015f, 0.07f) *
                            filter.Band(noise.Next(), 900.0f * std::exp(-t * 7.0f) + 180.0f, 0.35f, rate);
        }
      }
      Knock(out, 0.0f, 110.0f, 0.4f);  // the quay's stone
      Normalize(out.samples, 0.75f);
      return out.samples;
    }
    case Sound::ShortQuay: {  // a lonely horn sagging a tone, then two bass notes falling (E3, C3): not enough
      Buffer out(rate, 2.2f);
      Horn(out, 329.63f, 0.0f, 0.6f, 0.6f, 0.89f);
      UprightBass(out, kC3 * 1.25f, 0.55f, 0.6f, 0.4f);
      UprightBass(out, kC3, 0.85f, 0.7f, 0.4f);
      Normalize(out.samples, 0.75f);
      return out.samples;
    }
    case Sound::Gridlock: {  // horns honking into each other (a tritone, a minor second) over the grinding stop
      Buffer out(rate, 2.8f);
      GrindStop(out);
      const float at[] = {0.0f, 0.07f, 0.19f, 0.3f, 0.42f}, freq[] = {261.63f, 369.99f, 233.08f, 392.0f, 277.18f};
      for (int k = 0; k < 5; ++k) Horn(out, freq[k], at[k], 0.12f + 0.05f * static_cast<float>(k % 2), 0.32f);
      Normalize(out.samples, 0.85f);
      return out.samples;
    }
    case Sound::LastOrders: {  // after the last harbour's horn: a vibes run, the bass walking, brushes, the 6/9
      const float start = barges::kWarmLength;
      Buffer out(rate, start + 4.4f);
      for (int k = 0; k < 7; ++k) Vibes(out, kC4 * ClimbPitch(k), start + 0.125f * static_cast<float>(k), 0.35f, 0.5f);
      const float walk[] = {1.0f, 1.125f, 1.25f, 1.5f};  // C D E G, a quarter note each
      for (int k = 0; k < 4; ++k) {
        UprightBass(out, kC3 * walk[k], start + 0.25f * static_cast<float>(k), 0.45f, 0.35f);
        Brush(out, start + 0.25f * static_cast<float>(k), k % 2 ? 0.35f : 0.2f);
      }
      const float chord = start + 1.0f;
      for (int k = 0; k < 5; ++k) Vibes(out, kC4 * kSixNine[k], chord + 0.03f * static_cast<float>(k), 0.3f, 1.0f);
      UprightBass(out, kC3, chord, 0.6f, 0.6f);
      Brush(out, chord, 0.4f, 0.1f);
      Cymbal(out, chord, chord + 0.05f, chord + 2.2f, 0.2f);
      Normalize(out.samples, 0.85f);
      return out.samples;
    }
    case Sound::RushHour: {  // the city's light runs out: horns answer across it, the bass walks up, the chord at
                             // kCityLength
      const float full = barges::kCityLength;
      Buffer out(rate, full + 3.8f);
      // Near horns first, then farther and softer (pentatonic, so the answers are in key).
      const float horn_at[] = {0.15f, 0.6f, 1.0f, 1.4f, 1.75f, 2.1f, 2.4f};
      const float horn_freq[] = {196.0f, 293.66f, 220.0f, 329.63f, 261.63f, 392.0f, 293.66f};
      for (int k = 0; k < 7; ++k) Horn(out, horn_freq[k], horn_at[k], 0.32f, 0.32f - 0.025f * static_cast<float>(k));
      for (int k = 0; k < 12; ++k) {  // eighths up the pentatonic, the brushes building under them
        const float at = 0.25f * static_cast<float>(k);
        UprightBass(out, kC3 * ClimbPitch(k), at, 0.3f, 0.3f);
        Brush(out, at, 0.08f + 0.025f * static_cast<float>(k));
      }
      for (int k = 0; k < 5; ++k) Vibes(out, kC4 * kSixNine[k], full + 0.02f * static_cast<float>(k), 0.35f, 1.2f);
      for (float f : {1.0f, 1.25f, 1.5f}) Rhodes(out, kC5 * f, full, 0.2f, 1.0f);
      UprightBass(out, kC3, full, 0.6f, 0.7f);
      UprightBass(out, kC3 * 1.5f, full, 0.4f, 0.7f);
      for (float f : {261.63f, 329.63f, 392.0f}) Horn(out, f, full, 0.9f, 0.22f);  // every horn together
      Cymbal(out, full, full + 0.01f, full + 2.6f, 0.45f);
      Normalize(out.samples, 0.85f);
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

Sound NoteFor(SoundSet set, Color color) {
  if (set == SoundSet::Lantern && color == Color::Blue) return Sound::HarmonicaNote;
  if (set == SoundSet::Lantern && color == Color::Amber) return Sound::CelestaNote;
  if (set == SoundSet::Shop && color == Color::Blue) return Sound::MarimbaNote;
  if (set == SoundSet::Shop && color == Color::Amber) return Sound::PizzNote;
  if (set == SoundSet::Canal && color == Color::Blue) return Sound::VibesNote;
  if (set == SoundSet::Canal && color == Color::Amber) return Sound::BassNote;
  if (set == SoundSet::Canal && color == Color::Green) return Sound::RhodesNote;
  switch (color) {
    case Color::Blue: return Sound::NoteBlue;
    case Color::Amber: return Sound::NoteAmber;
    case Color::Green: return Sound::NoteGreen;
    case Color::Rose: return Sound::NoteRose;
    default: return Sound::NotePearl;
  }
}

Sound ChordFor(SoundSet set, Color color) {
  if (set == SoundSet::Lantern && color == Color::Blue) return Sound::LanternBlue;
  if (set == SoundSet::Lantern && color == Color::Amber) return Sound::LanternAmber;
  if (set == SoundSet::Shop && color == Color::Blue) return Sound::PaidBlue;
  if (set == SoundSet::Shop && color == Color::Amber) return Sound::PaidAmber;
  if (set == SoundSet::Canal && color == Color::Blue) return Sound::HarbourBlue;
  if (set == SoundSet::Canal && color == Color::Amber) return Sound::HarbourAmber;
  if (set == SoundSet::Canal && color == Color::Green) return Sound::HarbourGreen;
  switch (color) {
    case Color::Blue: return Sound::ChordBlue;
    case Color::Amber: return Sound::ChordAmber;
    case Color::Green: return Sound::ChordGreen;
    case Color::Rose: return Sound::ChordRose;
    default: return Sound::ChordPearl;
  }
}

const char* SoundName(Sound sound) {
  static constexpr const char* kNames[] = {
      "NoteBlue",      "NoteAmber",    "NoteGreen",    "NoteRose",     "NotePearl",   "ChordBlue",  "ChordAmber",
      "ChordGreen",    "ChordRose",    "ChordPearl",   "Thud",         "Splash",      "Chime",      "Grind",
      "Fanfare",       "Star",         "Pour",         "Click",        "Place",       "Lift",       "Tap",
      "HarmonicaNote", "CelestaNote",  "LanternBlue",  "LanternAmber", "PaneChime",   "Heart",      "RoseWindow",
      "Smoke",         "Drip",         "CruciblePour", "Ascent",       "MarimbaNote", "PizzNote",   "PaidBlue",
      "PaidAmber",     "Sludge",       "JarTap",       "SpillWood",    "ShortDull",   "RackClack",  "Till",
      "MarketDay",     "CorkPop",      "ShopBell",     "VibesNote",    "BassNote",    "RhodesNote", "HarbourBlue",
      "HarbourAmber",  "HarbourGreen", "LockBrush",    "MillClunk",    "GateClank",   "Murk",       "Wash",
      "ShortQuay",     "Gridlock",     "LastOrders",   "RushHour"};
  static_assert(std::size(kNames) == static_cast<size_t>(Sound::Count));
  return sound < Sound::Count ? kNames[static_cast<size_t>(sound)] : "?";
}

Sound MudFor(SoundSet set) {
  switch (set) {
    case SoundSet::Lantern: return Sound::Smoke;
    case SoundSet::Shop: return Sound::Sludge;
    case SoundSet::Canal: return Sound::Murk;
    default: return Sound::Thud;
  }
}

Sound PourFor(SoundSet set) {
  switch (set) {
    case SoundSet::Lantern: return Sound::CruciblePour;
    case SoundSet::Shop: return Sound::JarTap;
    case SoundSet::Canal: return Sound::LockBrush;
    default: return Sound::Pour;
  }
}

Sound SolvedFor(SoundSet set, bool grand) {
  if (set == SoundSet::Shop) return grand ? Sound::MarketDay : Sound::Till;
  if (set == SoundSet::Canal) return grand ? Sound::RushHour : Sound::LastOrders;
  if (set != SoundSet::Lantern) return Sound::Fanfare;
  return grand ? Sound::RoseWindow : Sound::Heart;
}

Sound JamFor(SoundSet set) { return set == SoundSet::Canal ? Sound::Gridlock : Sound::Grind; }

Sound SpillFor(SoundSet set) {
  switch (set) {
    case SoundSet::Shop: return Sound::SpillWood;
    case SoundSet::Canal: return Sound::Wash;
    default: return Sound::Splash;
  }
}

Sound ShortFor(SoundSet set) {
  switch (set) {
    case SoundSet::Shop: return Sound::ShortDull;
    case SoundSet::Canal: return Sound::ShortQuay;
    default: return Sound::Count;
  }
}

Sound TurnFor(SoundSet set) { return set == SoundSet::Shop ? Sound::RackClack : Sound::Click; }
Sound MillFor(SoundSet set) { return set == SoundSet::Canal ? Sound::MillClunk : Sound::Click; }
Sound GateFor(SoundSet set) { return set == SoundSet::Canal ? Sound::GateClank : Sound::Click; }

float HeartOnset(int j) { return -std::log(1.0f - 0.98f * static_cast<float>(std::clamp(j, 0, 11)) / 12.0f) / 3.5f; }

float ClimbPitch(int step) {
  // C major pentatonic, climbing (C D E G A, then up an octave).
  static constexpr float kScale[] = {1.0f, 1.125f, 1.25f, 1.5f, 1.6667f};
  step = std::max(step, 0);
  const int octave = std::min(step / 5, 2);
  return kScale[step % 5] * static_cast<float>(1 << octave);
}

}  // namespace threadlock::synth
