#pragma once

#include <array>
#include <atomic>
#include <cstdint>

// The quiet bed under a World 1 level (DESIGN.md §9): dawn in the tower. A slow glass pad walks
// Cadd9 -> Am7 -> Fmaj7 -> G6 (a chord every 8 s, each fading into the next; every chord holds
// the machine's C pentatonic notes well, so nothing the board plays clashes), over filtered
// "air" noise that swells slowly. Made in real time (no memory, endless): wavetable sines, cheap
// enough for the audio thread.
//
// Plain C++ (no engine): Render runs on the audio thread, SetTarget on the game thread. The gain
// ramps per sample towards the target, so ducking and fades never click.
namespace threadlock {

class Ambience {
 public:
  explicit Ambience(uint32_t sample_rate);

  // Interleaved stereo, written (not added).
  void Render(float* stereo, uint32_t frames);

  // Ramp to `gain` (0..1) over `seconds`.
  void SetTarget(float gain, float seconds);

  static constexpr float kChordSeconds = 8.0f;
  static constexpr int kVoices = 5;

 private:
  static constexpr int kTable = 2048;

  float Sine(float phase) const;  // phase in turns, [0, 1)

  uint32_t rate_;
  std::array<float, kTable + 1> table_{};
  std::atomic<float> target_{0.0f};
  std::atomic<float> step_{0.0f};  // gain change per sample
  float gain_ = 0.0f;
  uint64_t frame_ = 0;
  std::array<float, 2 * kVoices * 2> phase_{};  // [layer][voice][channel]
  uint32_t noise_[2] = {0x9E3779B9u, 0x7F4A7C15u};
  float low_[2] = {}, band_[2] = {};
};

}  // namespace threadlock
