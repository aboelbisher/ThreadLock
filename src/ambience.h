#pragma once

#include <array>
#include <atomic>
#include <cstdint>

// The quiet bed under a level (DESIGN.md §9). Made in real time (no memory, endless): wavetable
// sines, cheap enough for the audio thread. The beds share one chord walk, Cadd9 -> Am7 -> Fmaj7 -> G6
// (a chord every 8 s, each fading into the next; every chord holds the machine's C pentatonic notes
// well, so nothing the board plays clashes):
// - Lantern (World 1, and the map): dawn in the tower. A slow glass pad over filtered "air" noise that
//   swells slowly.
// - Shop (World 2): market day outside the apothecary. A low bowed-string pad (a few harmonics, a slow
//   vibrato), the street's murmur (noise in a voice's band, swelling, fluttering like talk), and a far
//   bell every 20 s.
// - Canal (World 3): the canals late at night. No pad: water lapping at the quays (low noise in slow,
//   uneven slaps, a little trickle on top), a soft brush swirling on a snare, a plucked bass playing each
//   chord's root and its fifth halfway, and a boat's horn far off every 24 s.
//
// Plain C++ (no engine): Render runs on the audio thread, SetTarget and SetBed on the game thread. The
// gain ramps per sample towards the target, so ducking and fades never click; a new bed fades the old
// one out first.
namespace threadlock {

class Ambience {
 public:
  enum class Bed : uint8_t { Lantern, Shop, Canal };

  explicit Ambience(uint32_t sample_rate);

  // Interleaved stereo, written (not added).
  void Render(float* stereo, uint32_t frames);

  // Ramp to `gain` (0..1) over `seconds`.
  void SetTarget(float gain, float seconds);
  // Switch beds: the playing one fades out (kSwitchSeconds), the new one fades in. Silent: at once.
  void SetBed(Bed bed) { wanted_.store(bed); }

  static constexpr float kChordSeconds = 8.0f;
  static constexpr float kBellSeconds = 20.0f;                      // the shop's far bell
  static constexpr float kHornSeconds = 24.0f, kHornFirst = 11.0f;  // the canal's far horn
  static constexpr float kSwitchSeconds = 0.4f;
  static constexpr int kVoices = 5;

 private:
  static constexpr int kTable = 2048;

  float Sine(float phase) const;  // phase in turns, [0, 1)
  float Bell(uint64_t frame, int channel) const;
  float Horn(uint64_t frame) const;
  float Bass(float into, const float* notes) const;

  uint32_t rate_;
  std::array<float, kTable + 1> table_{};
  std::atomic<float> target_{0.0f};
  std::atomic<float> step_{0.0f};  // gain change per sample
  std::atomic<Bed> wanted_{Bed::Lantern};
  Bed bed_ = Bed::Lantern;
  float gain_ = 0.0f;
  float switch_ = 1.0f;  // 0..1: the bed's own fade while switching
  uint64_t frame_ = 0;
  std::array<float, 2 * kVoices * 2> phase_{};  // [layer][voice][channel]
  std::array<float, kVoices> vibrato_{};        // the strings' vibrato phases
  uint32_t noise_[2] = {0x9E3779B9u, 0x7F4A7C15u};
  float low_[2] = {}, band_[2] = {};
  float flutter_[2] = {};                             // the murmur's talk: very low-passed noise
  float trickle_low_[2] = {}, trickle_band_[2] = {};  // the canal: the water's high band
  float brush_low_[2] = {}, brush_band_[2] = {};      // the brushes
};

}  // namespace threadlock
