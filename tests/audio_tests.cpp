// The synthesized sounds (audio_synth.h), the rose window's panes (rose_window.h) and the ambience
// (ambience.h): plain C++, so they're checked here without an audio device.
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "../src/ambience.h"
#include "../src/apothecary_look.h"
#include "../src/audio_synth.h"
#include "../src/barges.h"
#include "../src/rose_window.h"
#include "../src/sigil_look.h"
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
      const std::string name = synth::SoundName(sound);
      CAPTURE(name);
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

namespace {

// The loudness (RMS) of `samples` between `from` and `to` seconds.
float Rms(const std::vector<float>& samples, float from, float to) {
  const size_t a = static_cast<size_t>(from * kRate), b = std::min(samples.size(), static_cast<size_t>(to * kRate));
  double sum = 0.0;
  for (size_t i = a; i < b; ++i) sum += static_cast<double>(samples[i]) * samples[i];
  return b > a ? static_cast<float>(std::sqrt(sum / static_cast<double>(b - a))) : 0.0f;
}

// Renders `seconds` of `ambience` in the mixer's blocks: the peak, the largest step between samples, finite.
struct Bed {
  float peak = 0.0f, step = 0.0f;
  bool finite = true;
  std::vector<float> left;  // the left channel, for timing
};
Bed RenderBed(Ambience& ambience, float seconds) {
  Bed bed;
  std::vector<float> block(512 * 2);
  float last[2] = {0.0f, 0.0f};
  for (int b = 0; b < static_cast<int>(seconds * kRate) / 512; ++b) {
    ambience.Render(block.data(), 512);
    for (size_t i = 0; i < block.size(); ++i) {
      bed.finite = bed.finite && std::isfinite(block[i]);
      bed.peak = std::max(bed.peak, std::fabs(block[i]));
      if (b > 0 || i >= 2) bed.step = std::max(bed.step, std::fabs(block[i] - last[i % 2]));  // not across renders
      last[i % 2] = block[i];
      if (i % 2 == 0) bed.left.push_back(block[i]);
    }
  }
  return bed;
}

}  // namespace

TEST_SUITE("audio") {
  TEST_CASE("the Shop set's voices") {
    using synth::Sound;
    using synth::SoundSet;
    CHECK(synth::NoteFor(SoundSet::Shop, Color::Blue) == Sound::MarimbaNote);
    CHECK(synth::NoteFor(SoundSet::Shop, Color::Amber) == Sound::PizzNote);
    CHECK(synth::NoteFor(SoundSet::Shop, Color::Rose) == Sound::NoteRose);  // no shop voice: the Workshop's
    CHECK(synth::ChordFor(SoundSet::Shop, Color::Blue) == Sound::PaidBlue);
    CHECK(synth::ChordFor(SoundSet::Shop, Color::Amber) == Sound::PaidAmber);
    CHECK(synth::ChordFor(SoundSet::Shop, Color::Green) == Sound::ChordGreen);
    CHECK(synth::MudFor(SoundSet::Shop) == Sound::Sludge);
    CHECK(synth::PourFor(SoundSet::Shop) == Sound::JarTap);
    CHECK(synth::SolvedFor(SoundSet::Shop, false) == Sound::Till);
    CHECK(synth::SolvedFor(SoundSet::Shop, true) == Sound::MarketDay);
  }

  TEST_CASE("an order paid: the bell and the stamp sound when the art shows them") {
    for (synth::Sound sound : {synth::Sound::PaidBlue, synth::Sound::PaidAmber}) {
      const std::string name = synth::SoundName(sound);
      CAPTURE(name);
      const std::vector<float> paid = synth::Make(sound, kRate);
      // Each strike is much louder just after its time than just before.
      for (float at : {apothecary::kPaidBell, apothecary::kPaidStamp}) {
        CAPTURE(at);
        CHECK(Rms(paid, at + 0.002f, at + 0.04f) > 1.5f * Rms(paid, at - 0.05f, at - 0.005f));
      }
      CHECK(Rms(paid, 0.0f, 0.03f) > 0.05f);  // the cork, at once
      CHECK(synth::Make(sound, kRate) == paid);
    }
    // The till waits for the last order's stamp.
    const std::vector<float> till = synth::Make(synth::Sound::Till, kRate);
    CHECK(Peak(std::vector<float>(till.begin(), till.begin() + static_cast<long>(apothecary::kPaidStamp * kRate))) ==
          0.0f);
    CHECK(synth::Make(synth::Sound::MarketDay, kRate).size() > till.size());
  }

  TEST_CASE("the shop's ambience: quiet, finite, click-free, a far bell") {
    Ambience ambience(kRate);
    ambience.SetBed(Ambience::Bed::Shop);
    ambience.SetTarget(1.0f, 2.0f);
    const Bed bed = RenderBed(ambience, 45.0f);
    CHECK(bed.finite);
    CHECK(bed.peak > 0.02f);
    CHECK(bed.peak <= 0.3f);
    CHECK(bed.step < 0.02f);
    // The bell rings at 6 s and 26 s: louder just after than just before.
    for (float at : {6.0f, 26.0f}) {
      CAPTURE(at);
      CHECK(Rms(bed.left, at + 0.01f, at + 0.3f) > 1.3f * Rms(bed.left, at - 0.6f, at - 0.05f));
    }
  }

  TEST_CASE("switching beds fades through, without a click") {
    Ambience ambience(kRate);
    ambience.SetTarget(1.0f, 0.5f);
    RenderBed(ambience, 4.0f);  // the Lantern bed, playing
    ambience.SetBed(Ambience::Bed::Shop);
    const Bed bed = RenderBed(ambience, 4.0f);
    CHECK(bed.finite);
    CHECK(bed.step < 0.02f);
    // Near silence where one bed hands over to the other (0.4 s in).
    const float handover = Rms(bed.left, Ambience::kSwitchSeconds - 0.01f, Ambience::kSwitchSeconds + 0.01f);
    CHECK(handover < 0.25f * Rms(bed.left, 2.0f, 3.0f));
  }
}

TEST_SUITE("audio") {
  TEST_CASE("the Canal set's voices") {
    using synth::Sound;
    using synth::SoundSet;
    CHECK(synth::NoteFor(SoundSet::Canal, Color::Blue) == Sound::VibesNote);
    CHECK(synth::NoteFor(SoundSet::Canal, Color::Amber) == Sound::BassNote);
    CHECK(synth::NoteFor(SoundSet::Canal, Color::Green) == Sound::RhodesNote);  // World 3 has green harbours
    CHECK(synth::NoteFor(SoundSet::Canal, Color::Rose) == Sound::NoteRose);
    CHECK(synth::ChordFor(SoundSet::Canal, Color::Blue) == Sound::HarbourBlue);
    CHECK(synth::ChordFor(SoundSet::Canal, Color::Amber) == Sound::HarbourAmber);
    CHECK(synth::ChordFor(SoundSet::Canal, Color::Green) == Sound::HarbourGreen);
    CHECK(synth::MudFor(SoundSet::Canal) == Sound::Murk);
    CHECK(synth::PourFor(SoundSet::Canal) == Sound::LockBrush);
    CHECK(synth::SolvedFor(SoundSet::Canal, false) == Sound::LastOrders);
    CHECK(synth::SolvedFor(SoundSet::Canal, true) == Sound::RushHour);
    CHECK(synth::JamFor(SoundSet::Canal) == Sound::Gridlock);
    CHECK(synth::SpillFor(SoundSet::Canal) == Sound::Wash);
    CHECK(synth::ShortFor(SoundSet::Canal) == Sound::ShortQuay);
    CHECK(synth::MillFor(SoundSet::Canal) == Sound::MillClunk);
    CHECK(synth::GateFor(SoundSet::Canal) == Sound::GateClank);
    // The other sets keep theirs.
    CHECK(synth::JamFor(SoundSet::Shop) == Sound::Grind);
    CHECK(synth::SpillFor(SoundSet::Shop) == Sound::SpillWood);
    CHECK(synth::SpillFor(SoundSet::Lantern) == Sound::Splash);
    CHECK(synth::ShortFor(SoundSet::Shop) == Sound::ShortDull);
    CHECK(synth::ShortFor(SoundSet::Workshop) == Sound::Count);
    CHECK(synth::TurnFor(SoundSet::Shop) == Sound::RackClack);
    CHECK(synth::MillFor(SoundSet::Lantern) == Sound::Click);
    CHECK(synth::GateFor(SoundSet::Shop) == Sound::Click);
  }

  TEST_CASE("a harbour served: the horn as its lamps come on, quiet once the district's light has run") {
    for (synth::Sound sound : {synth::Sound::HarbourBlue, synth::Sound::HarbourAmber, synth::Sound::HarbourGreen}) {
      const std::string name = synth::SoundName(sound);
      CAPTURE(name);
      const std::vector<float> harbour = synth::Make(sound, kRate);
      CHECK(Rms(harbour, 0.0f, 0.1f) > 0.05f);  // the horn, at once
      CHECK(Rms(harbour, barges::kDistrictLength + 0.15f, 99.0f) < 0.1f * Rms(harbour, 0.1f, 1.0f));
      CHECK(synth::Make(sound, kRate) == harbour);
    }
  }

  TEST_CASE("the canal's solved runs: after the last horn, and Rush Hour's chord with the city's light") {
    const std::vector<float> last = synth::Make(synth::Sound::LastOrders, kRate);
    CHECK(Peak(std::vector<float>(last.begin(), last.begin() + static_cast<long>(barges::kWarmLength * kRate))) ==
          0.0f);
    const std::vector<float> rush = synth::Make(synth::Sound::RushHour, kRate);
    const float full = barges::kCityLength;
    CHECK(Rms(rush, full + 0.01f, full + 0.2f) > 1.5f * Rms(rush, full - 0.3f, full - 0.05f));
    CHECK(Rms(rush, 0.0f, 0.5f) > 0.02f);  // the city answers at once
    CHECK(rush.size() > last.size());
  }

  TEST_CASE("the canal's ambience: quiet, finite, click-free, a far horn") {
    Ambience ambience(kRate);
    ambience.SetBed(Ambience::Bed::Canal);
    ambience.SetTarget(1.0f, 2.0f);
    const Bed bed = RenderBed(ambience, 40.0f);
    CHECK(bed.finite);
    CHECK(bed.peak > 0.02f);
    CHECK(bed.peak <= 0.3f);
    CHECK(bed.step < 0.02f);
    // The horn sounds at 11 s and 35 s: louder while it holds than just before.
    for (float at : {Ambience::kHornFirst, Ambience::kHornFirst + Ambience::kHornSeconds}) {
      CAPTURE(at);
      CHECK(Rms(bed.left, at + 0.5f, at + 1.5f) > 1.3f * Rms(bed.left, at - 0.8f, at - 0.05f));
    }
  }

  TEST_CASE("switching to the canal's bed fades through, without a click") {
    Ambience ambience(kRate);
    ambience.SetBed(Ambience::Bed::Shop);
    ambience.SetTarget(1.0f, 0.5f);
    RenderBed(ambience, 4.0f);
    ambience.SetBed(Ambience::Bed::Canal);
    const Bed bed = RenderBed(ambience, 4.0f);
    CHECK(bed.finite);
    CHECK(bed.step < 0.02f);
    const float handover = Rms(bed.left, Ambience::kSwitchSeconds - 0.01f, Ambience::kSwitchSeconds + 0.01f);
    CHECK(handover < 0.25f * Rms(bed.left, 2.0f, 3.0f));
  }
}

TEST_SUITE("audio") {
  using synth::Sound;
  using synth::SoundSet;
  TEST_CASE("the Sanctum set's voices") {
    CHECK(synth::NoteFor(SoundSet::Sanctum, Color::Blue) == Sound::HarpNote);
    CHECK(synth::NoteFor(SoundSet::Sanctum, Color::Amber) == Sound::ChoirNote);
    CHECK(synth::NoteFor(SoundSet::Sanctum, Color::Green) == Sound::OrganNote);
    CHECK(synth::NoteFor(SoundSet::Sanctum, Color::Rose) == Sound::BowlNote);
    CHECK(synth::NoteFor(SoundSet::Sanctum, Color::Pearl) == Sound::PearlNote);
    CHECK(synth::ChordFor(SoundSet::Sanctum, Color::Blue) == Sound::PhialBlue);
    CHECK(synth::ChordFor(SoundSet::Sanctum, Color::Amber) == Sound::PhialAmber);
    CHECK(synth::ChordFor(SoundSet::Sanctum, Color::Green) == Sound::PhialGreen);
    CHECK(synth::ChordFor(SoundSet::Sanctum, Color::Rose) == Sound::PhialRose);
    CHECK(synth::ChordFor(SoundSet::Sanctum, Color::Pearl) == Sound::PhialPearl);
    CHECK(synth::MudFor(SoundSet::Sanctum) == Sound::Clot);
    CHECK(synth::PourFor(SoundSet::Sanctum) == Sound::FlaskPour);
    CHECK(synth::SolvedFor(SoundSet::Sanctum, false) == Sound::Amen);
    CHECK(synth::SolvedFor(SoundSet::Sanctum, true) == Sound::GreatWork);
    CHECK(synth::JamFor(SoundSet::Sanctum) == Sound::SealCrack);
    CHECK(synth::SpillFor(SoundSet::Sanctum) == Sound::PhialSpill);
    CHECK(synth::ShortFor(SoundSet::Sanctum) == Sound::HollowPhial);
    CHECK(synth::TurnFor(SoundSet::Sanctum) == Sound::Click);
    CHECK(synth::MillFor(SoundSet::Sanctum) == Sound::Click);
    CHECK(synth::GateFor(SoundSet::Sanctum) == Sound::Click);
    auto mix = [](SoundSet set, Color made, Sound sound, float pitch) {
      const synth::MixVoice voice = synth::MixFor(set, made);
      CHECK(voice.sound == sound);
      CHECK(voice.pitch == doctest::Approx(pitch));
    };
    mix(SoundSet::Sanctum, Color::Green, Sound::MixSwell, 1.0f);
    mix(SoundSet::Sanctum, Color::Pearl, Sound::MixSwell, 1.5f);
    mix(SoundSet::Sanctum, Color::Mud, Sound::Clot, 1.0f);
    mix(SoundSet::Sanctum, Color::Blue, Sound::MixSwell, 1.0f);
    // Elsewhere every mix is the set's green note at 0.75, as before.
    mix(SoundSet::Canal, Color::Green, Sound::RhodesNote, 0.75f);
    mix(SoundSet::Workshop, Color::Mud, Sound::NoteGreen, 0.75f);
    mix(SoundSet::Shop, Color::Pearl, Sound::NoteGreen, 0.75f);
    mix(SoundSet::Lantern, Color::Green, Sound::NoteGreen, 0.75f);
    CHECK(synth::JamFor(SoundSet::Canal) == Sound::Gridlock);
    CHECK(synth::MudFor(SoundSet::Canal) == Sound::Murk);
    CHECK(synth::SolvedFor(SoundSet::Canal, true) == Sound::RushHour);
  }

  TEST_CASE("a phial full: the glass rings at once, the choir swells with the ignite, quiet once it's lit") {
    for (Sound sound : {Sound::PhialBlue, Sound::PhialAmber, Sound::PhialGreen, Sound::PhialRose, Sound::PhialPearl}) {
      const std::string name = synth::SoundName(sound);
      CAPTURE(name);
      const std::vector<float> phial = synth::Make(sound, kRate);
      CHECK(Rms(phial, 0.0f, 0.05f) > 0.05f);
      CHECK(Rms(phial, 1.0f, 1.5f) > 0.1f * Rms(phial, 0.0f, 0.5f));
      CHECK(Rms(phial, sigil::kIgniteStart + 1.85f, 99.0f) < 0.1f * Rms(phial, 0.1f, 1.0f));
      CHECK(synth::Make(sound, kRate) == phial);
    }
  }

  TEST_CASE("the sanctum's solved runs: the amen as the sigil ignites, the Great Work's chord when both are lit") {
    const std::vector<float> amen = synth::Make(Sound::Amen, kRate);
    const long ignite = static_cast<long>(sigil::kIgniteStart * kRate);
    CHECK(Peak(std::vector<float>(amen.begin(), amen.begin() + ignite)) == 0.0f);
    const float m = synth::kAmenFinal;
    CHECK(Rms(amen, m + 0.15f, m + 0.5f) > 1.3f * Rms(amen, m - 0.12f, m - 0.02f));  // the "-men" after the breath
    CHECK(Rms(amen, sigil::kSolvedLength + 0.7f, 99.0f) < 0.1f * Rms(amen, m, m + 0.8f));
    const std::vector<float> great = synth::Make(Sound::GreatWork, kRate);
    CHECK(Peak(std::vector<float>(great.begin(), great.begin() + ignite)) == 0.0f);
    const float full = sigil::kGrandLength;
    CHECK(Rms(great, full + 0.01f, full + 0.2f) > 1.5f * Rms(great, full - 0.3f, full - 0.05f));
    CHECK(Rms(great, 0.8f, 1.6f) > 0.01f);  // the pedal swelling
    CHECK(great.size() > amen.size());
  }

  TEST_CASE("the sanctum's moments") {
    const std::vector<float> bell = synth::Make(Sound::SealBell, kRate);
    CHECK(Rms(bell, 0.0f, 0.03f) > 0.05f);  // on the release tick, no delay
    CHECK(Rms(bell, 1.0f, 1.5f) > 0.03f * Rms(bell, 0.0f, 0.3f));
    const std::vector<float> clot = synth::Make(Sound::Clot, kRate);
    CHECK(Rms(clot, 1.5f, 99.0f) < 0.1f * Rms(clot, 0.0f, 0.6f));
    const std::vector<float> crack = synth::Make(Sound::SealCrack, kRate);
    CHECK(Rms(crack, 0.0f, 0.03f) > 0.05f);
    CHECK(Rms(crack, 1.0f, 1.5f) > 0.1f * Rms(crack, 0.0f, 0.5f));
    const std::vector<float> hollow = synth::Make(Sound::HollowPhial, kRate);
    CHECK(Rms(hollow, 0.422f, 0.46f) > 1.3f * Rms(hollow, 0.38f, 0.415f));
    for (Sound sound : {Sound::Clot, Sound::BowlNote, Sound::MixSwell, Sound::ChoirNote, Sound::GreatWork})
      CHECK(synth::Make(sound, kRate) == synth::Make(sound, kRate));
  }

  TEST_CASE("the sanctum's ambience: quiet, finite, click-free, a far choir and a low bell") {
    Ambience ambience(kRate);
    ambience.SetBed(Ambience::Bed::Sanctum);
    ambience.SetTarget(1.0f, 2.0f);
    const Bed bed = RenderBed(ambience, 50.0f);
    CHECK(bed.finite);
    CHECK(bed.peak > 0.02f);
    CHECK(bed.peak <= 0.3f);
    CHECK(bed.step < 0.02f);
    for (float at : {Ambience::kChoirFirst, Ambience::kChoirFirst + Ambience::kChoirSeconds}) {
      CAPTURE(at);
      CHECK(Rms(bed.left, at + 2.0f, at + 3.0f) > 1.3f * Rms(bed.left, at - 0.8f, at - 0.05f));
    }
    for (float at : {Ambience::kLowBellFirst, Ambience::kLowBellFirst + Ambience::kLowBellSeconds}) {
      CAPTURE(at);
      CHECK(Rms(bed.left, at + 0.01f, at + 0.3f) > 1.3f * Rms(bed.left, at - 0.6f, at - 0.05f));
    }
  }

  TEST_CASE("switching to the sanctum's bed fades through, without a click") {
    Ambience ambience(kRate);
    ambience.SetBed(Ambience::Bed::Canal);
    ambience.SetTarget(1.0f, 0.5f);
    RenderBed(ambience, 4.0f);
    ambience.SetBed(Ambience::Bed::Sanctum);
    const Bed bed = RenderBed(ambience, 4.0f);
    CHECK(bed.finite);
    CHECK(bed.step < 0.02f);
    const float handover = Rms(bed.left, Ambience::kSwitchSeconds - 0.01f, Ambience::kSwitchSeconds + 0.01f);
    CHECK(handover < 0.25f * Rms(bed.left, 2.0f, 3.0f));
  }
}

TEST_SUITE("audio") {
  TEST_CASE("each sound belongs to a set: every pick comes from its set or the Workshop's") {
    using synth::Sound;
    using synth::SoundSet;
    CHECK(synth::SetOf(Sound::NoteBlue) == SoundSet::Workshop);
    CHECK(synth::SetOf(Sound::Tap) == SoundSet::Workshop);
    CHECK(synth::SetOf(Sound::HarmonicaNote) == SoundSet::Lantern);
    CHECK(synth::SetOf(Sound::Ascent) == SoundSet::Lantern);
    CHECK(synth::SetOf(Sound::MarimbaNote) == SoundSet::Shop);
    CHECK(synth::SetOf(Sound::ShopBell) == SoundSet::Shop);
    CHECK(synth::SetOf(Sound::VibesNote) == SoundSet::Canal);
    CHECK(synth::SetOf(Sound::RushHour) == SoundSet::Canal);
    CHECK(synth::SetOf(Sound::HarpNote) == SoundSet::Sanctum);
    CHECK(synth::SetOf(Sound::Kindle) == SoundSet::Sanctum);
    for (SoundSet set : {SoundSet::Workshop, SoundSet::Lantern, SoundSet::Shop, SoundSet::Canal, SoundSet::Sanctum}) {
      std::vector<Sound> picks{synth::MudFor(set),          synth::PourFor(set), synth::SolvedFor(set, false),
                               synth::SolvedFor(set, true), synth::JamFor(set),  synth::SpillFor(set),
                               synth::TurnFor(set),         synth::MillFor(set), synth::GateFor(set)};
      if (synth::ShortFor(set) != Sound::Count) picks.push_back(synth::ShortFor(set));
      for (Color c : {Color::Blue, Color::Amber, Color::Green, Color::Rose, Color::Pearl, Color::Mud}) {
        picks.push_back(synth::NoteFor(set, c));
        picks.push_back(synth::ChordFor(set, c));
        picks.push_back(synth::MixFor(set, c).sound);
      }
      for (Sound pick : picks) {
        const std::string name = synth::SoundName(pick);
        CAPTURE(name);
        CHECK((synth::SetOf(pick) == set || synth::SetOf(pick) == SoundSet::Workshop));
      }
    }
  }
}
