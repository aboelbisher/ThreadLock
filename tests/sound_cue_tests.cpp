// What a run sounds like (sound_cues.h): the voices per sound set, the rose window's pane chimes, the
// drip plinks, and the listener on a tall board.
#include <string>
#include <vector>

#include "../src/sound_cues.h"
#include "doctest.h"
#include "test_levels.h"

using namespace threadlock;
using synth::Sound;
using synth::SoundSet;

namespace {

// One spout pouring `beads` down a pipe (with an optional shape) into one lantern that wants 4 blue.
Level LanternLevel(const std::string& beads, const std::string& shape = "") {
  return MakeLevel(R"({"id": "c", "nodes": [{"id": "s", "kind": "spout", "at": [180, 60], "beads": ")" + beads +
                   R"("}, {"id": "c", "kind": "cup", "at": [180, 400], "want": "B", "count": 4}],
                   "pipes": [["s", "c", [[180, 120], [180, 300]])" +
                   shape + "]]}");
}

// Every cue of a whole run.
std::vector<Cue> RunCues(const Level& level, SoundSet set, const Listener& listener = {}) {
  Board board(level);
  board.Start();
  std::vector<Cue> all;
  while (board.Running()) {
    const std::vector<Event>& events = board.Tick();
    const Cues cues = CuesFor(events, level, board, set, listener);
    all.insert(all.end(), cues.sounds.begin(), cues.sounds.end());
  }
  return all;
}

int Count(const std::vector<Cue>& cues, Sound sound) {
  int n = 0;
  for (const Cue& cue : cues) n += cue.sound == sound;
  return n;
}

}  // namespace

TEST_SUITE("sound cues") {
  TEST_CASE("a lantern's panes chime as it fills, climbing round the window") {
    const Level level = LanternLevel("B*4");
    const std::vector<Cue> cues = RunCues(level, SoundSet::Lantern);
    CHECK(Count(cues, Sound::HarmonicaNote) == 4);
    CHECK(Count(cues, Sound::LanternBlue) == 1);
    REQUIRE(Count(cues, Sound::PaneChime) == 12);  // the only lantern owns every outer pane
    float last = 0.0f;
    int k = 0;
    for (const Cue& cue : cues) {
      if (cue.sound != Sound::PaneChime) continue;
      CHECK(cue.pitch == doctest::Approx(synth::ClimbPitch(k++)));
      CHECK(cue.pitch > last);
      last = cue.pitch;
    }
    CHECK(Count(cues, Sound::NoteBlue) == 0);
    CHECK(Count(cues, Sound::CruciblePour) == 4);
  }

  TEST_CASE("mud in a lantern stops its chimes") {
    const Level level = LanternLevel("B A B B");
    const std::vector<Cue> cues = RunCues(level, SoundSet::Lantern);
    CHECK(Count(cues, Sound::PaneChime) == 3);  // the first drop's panes, then nothing
    CHECK(Count(cues, Sound::Splash) >= 1);
  }

  TEST_CASE("a drip fall plinks once per drop") {
    const std::vector<Cue> cues = RunCues(LanternLevel("B*4", R"(, {"drip": 2})"), SoundSet::Lantern);
    CHECK(Count(cues, Sound::Drip) == 4);  // a funnel at the pipe's end, over the lantern
    CHECK(Count(RunCues(LanternLevel("B*4", R"(, {"drip": 1})"), SoundSet::Lantern), Sound::Drip) == 4);  // mid-pipe
    CHECK(Count(RunCues(LanternLevel("B*4"), SoundSet::Lantern), Sound::Drip) == 0);
  }

  TEST_CASE("the Workshop set sounds as before") {
    const std::vector<Cue> cues = RunCues(LanternLevel("B*4", R"(, {"drip": 2})"), SoundSet::Workshop);
    CHECK(Count(cues, Sound::NoteBlue) == 4);
    CHECK(Count(cues, Sound::ChordBlue) == 1);
    CHECK(Count(cues, Sound::Pour) == 4);
    CHECK(Count(cues, Sound::PaneChime) == 0);
    CHECK(Count(cues, Sound::Drip) == 0);
    CHECK(Count(cues, Sound::HarmonicaNote) == 0);
  }

  TEST_CASE("off the view sounds softer, failures don't") {
    const Listener listener{0.0f, 200.0f};  // shows y 0..200
    CHECK(ListenerGain(listener, 100.0f) == doctest::Approx(1.0f));
    CHECK(ListenerGain(listener, 200.0f) == doctest::Approx(1.0f));
    CHECK(ListenerGain(listener, 300.0f) == doctest::Approx(0.675f));
    CHECK(ListenerGain(listener, 400.0f) == doctest::Approx(0.35f));
    CHECK(ListenerGain(listener, 2000.0f) == doctest::Approx(0.35f));
    CHECK(ListenerGain({}, 5000.0f) == doctest::Approx(1.0f));  // a short board hears everything

    const Level level = LanternLevel("B*4");
    Board board(level);
    board.Start();
    const int cup = NodeOf(level, "c");  // at y 400: a view away
    const std::vector<Event> events = {{.type = EventType::NearMiss, .node = cup},
                                       {.type = EventType::Spill, .node = cup},
                                       {.type = EventType::Collide, .node = cup}};
    const Cues cues = CuesFor(events, level, board, SoundSet::Lantern, listener);
    REQUIRE(cues.sounds.size() == 3);
    CHECK(cues.sounds[0].volume == doctest::Approx(0.5f * 0.35f));
    CHECK(cues.sounds[1].volume == doctest::Approx(0.7f));
    CHECK(cues.sounds[2].sound == Sound::Smoke);
    CHECK(cues.sounds[2].volume == doctest::Approx(0.9f));
    CHECK(cues.buzzes.size() == 1);
  }
}
