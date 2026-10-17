// The synthesized sounds (audio_synth.h), the rose window's panes (rose_window.h) and the ambience
// (ambience.h): plain C++, so they're checked here without an audio device.
#include <algorithm>
#include <cmath>
#include <vector>

#include "../src/ambience.h"
#include "../src/audio_synth.h"
#include "../src/rose_window.h"
#include "doctest.h"

using namespace threadlock;

namespace {

constexpr uint32_t kRate = 48000;

float Peak(const std::vector<float>& samples, size_t from = 0) {
  float most = 0.0f;
  for (size_t i = from; i < samples.size(); ++i) most = std::max(most, std::fabs(samples[i]));
  return most;
}

}  // namespace

TEST_SUITE("audio") {
  TEST_CASE("every sound is finite, under full scale and dies away") {
    for (int s = 0; s < static_cast<int>(synth::Sound::Count); ++s) {
      const auto sound = static_cast<synth::Sound>(s);
      CAPTURE(synth::SoundName(sound));
      const std::vector<float> samples = synth::Make(sound, kRate);
      REQUIRE(samples.size() > 100);
      CHECK(std::all_of(samples.begin(), samples.end(), [](float v) { return std::isfinite(v); }));
      const float peak = Peak(samples);
      CHECK(peak > 0.1f);
      CHECK(peak <= 1.0f);
      CHECK(std::fabs(samples.back()) < 1e-3f);  // no click when it ends
      if (sound >= synth::Sound::HarmonicaNote) {
        // The Lantern set rings out on its own: its last 5% is already quiet.
        CHECK(Peak(samples, samples.size() - samples.size() / 20) < 0.08f * peak);
      }
    }
  }

  TEST_CASE("sounds are the same every time") {
    for (synth::Sound sound : {synth::Sound::Smoke, synth::Sound::RoseWindow, synth::Sound::CruciblePour}) {
      CHECK(synth::Make(sound, kRate) == synth::Make(sound, kRate));
    }
  }

  TEST_CASE("each set picks its own voices") {
    using synth::Sound;
    using synth::SoundSet;
    CHECK(synth::NoteFor(SoundSet::Lantern, Color::Blue) == Sound::HarmonicaNote);
    CHECK(synth::NoteFor(SoundSet::Lantern, Color::Amber) == Sound::CelestaNote);
    CHECK(synth::ChordFor(SoundSet::Lantern, Color::Blue) == Sound::LanternBlue);
    CHECK(synth::ChordFor(SoundSet::Lantern, Color::Amber) == Sound::LanternAmber);
    CHECK(synth::NoteFor(SoundSet::Lantern, Color::Green) == Sound::NoteGreen);  // no glass voice: the Workshop's
    CHECK(synth::NoteFor(SoundSet::Workshop, Color::Blue) == Sound::NoteBlue);
    CHECK(synth::ChordFor(SoundSet::Workshop, Color::Amber) == Sound::ChordAmber);
    CHECK(synth::MudFor(SoundSet::Lantern) == Sound::Smoke);
    CHECK(synth::MudFor(SoundSet::Workshop) == Sound::Thud);
    CHECK(synth::PourFor(SoundSet::Lantern) == Sound::CruciblePour);
    CHECK(synth::PourFor(SoundSet::Workshop) == Sound::Pour);
    CHECK(synth::SolvedFor(SoundSet::Lantern, false) == Sound::Heart);
    CHECK(synth::SolvedFor(SoundSet::Lantern, true) == Sound::RoseWindow);
    CHECK(synth::SolvedFor(SoundSet::Workshop, true) == Sound::Fanfare);
  }

  TEST_CASE("the heart's chimes fall when its panes start to light") {
    // LanternArt::UpdatePanes eases the first inner pane at 3.5/s and starts pane j when that one
    // reaches 0.98 j/12. Replay that at 240 fps.
    constexpr float kDt = 1.0f / 240.0f;
    float first = 0.0f, t = 0.0f;
    for (int j = 0; j < kInnerPanes; ++j) {
      while (first < 0.98f * static_cast<float>(j) / static_cast<float>(kInnerPanes)) {
        first += (1.0f - first) * kDt * 3.5f;
        t += kDt;
      }
      CAPTURE(j);
      CHECK(std::fabs(synth::HeartOnset(j) - t) < 0.02f);
    }
    CHECK(synth::HeartOnset(0) == 0.0f);
  }

  TEST_CASE("panes are shared out among the lanterns") {
    Level level;
    level.nodes.resize(5);
    level.nodes[0].kind = NodeKind::Spout;
    for (int n : {1, 3, 4}) level.nodes[static_cast<size_t>(n)].kind = NodeKind::Cup;
    level.nodes[2].kind = NodeKind::Fork;
    CHECK(LanternPanes(level, 1).first == 0);
    CHECK(LanternPanes(level, 1).last == 4);
    CHECK(LanternPanes(level, 3).first == 4);
    CHECK(LanternPanes(level, 4).last == 12);
    CHECK(LanternPanes(level, 2).Count() == 0);
    CHECK(PanesLit(3, 6, 4, false) == 2);
    CHECK(PanesLit(5, 6, 4, false) == 3);
    CHECK(PanesLit(9, 6, 4, false) == 4);
    CHECK(PanesLit(6, 6, 4, true) == 0);
    CHECK(PanesLit(1, 0, 4, false) == 0);
  }

  TEST_CASE("the ambience stays quiet, finite and click-free") {
    Ambience ambience(kRate);
    ambience.SetTarget(1.0f, 2.0f);
    std::vector<float> block(512 * 2);
    float peak = 0.0f, step = 0.0f, last[2] = {0.0f, 0.0f};
    bool finite = true;
    for (int b = 0; b < 60 * static_cast<int>(kRate) / 512; ++b) {
      if (b == 2000) ambience.SetTarget(0.6f, 1.0f);   // a duck
      if (b == 3000) ambience.SetTarget(1.0f, 0.01f);  // a fast change
      ambience.Render(block.data(), 512);
      for (size_t i = 0; i < block.size(); ++i) {
        finite = finite && std::isfinite(block[i]);
        peak = std::max(peak, std::fabs(block[i]));
        step = std::max(step, std::fabs(block[i] - last[i % 2]));
        last[i % 2] = block[i];
      }
    }
    CHECK(finite);
    CHECK(peak > 0.02f);
    CHECK(peak <= 0.3f);
    CHECK(step < 0.02f);
    ambience.SetTarget(0.0f, 0.5f);
    for (int b = 0; b < 60; ++b) ambience.Render(block.data(), 512);
    CHECK(Peak(block) == 0.0f);
  }
}
