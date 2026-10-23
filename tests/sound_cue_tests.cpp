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

TEST_SUITE("sound cues") {
  TEST_CASE("the Shop set: marimba, an order paid, jar taps") {
    const std::vector<Cue> cues = RunCues(LanternLevel("B*4"), SoundSet::Shop);
    CHECK(Count(cues, Sound::MarimbaNote) == 4);
    CHECK(Count(cues, Sound::PaidBlue) == 1);  // one sound: cork, chord, bell, stamp
    CHECK(Count(cues, Sound::JarTap) == 4);
    CHECK(Count(cues, Sound::NoteBlue) == 0);
    CHECK(Count(cues, Sound::PaneChime) == 0);
    CHECK(Count(cues, Sound::ShortDull) == 0);  // solved: nothing short
  }

  TEST_CASE("the Shop set: sludge, a spill on wood, a short order") {
    CHECK(Count(RunCues(LanternLevel("B*5"), SoundSet::Shop), Sound::SpillWood) == 1);
    CHECK(Count(RunCues(LanternLevel("B*5"), SoundSet::Shop), Sound::Splash) == 0);
    // Three of four: short at the end, one dull knock for the run, heard in full.
    const std::vector<Cue> short_run = RunCues(LanternLevel("B*3"), SoundSet::Shop, {0.0f, 100.0f});
    REQUIRE(Count(short_run, Sound::ShortDull) == 1);
    for (const Cue& cue : short_run) {
      if (cue.sound == Sound::ShortDull) CHECK(cue.volume == doctest::Approx(0.8f));
    }
    CHECK(Count(RunCues(LanternLevel("B*3"), SoundSet::Workshop), Sound::ShortDull) == 0);
    const Level level = LanternLevel("B*4");
    Board board(level);
    board.Start();
    const Cues mud = CuesFor({{.type = EventType::Collide, .node = NodeOf(level, "c")}}, level, board, SoundSet::Shop);
    REQUIRE(mud.sounds.size() == 1);
    CHECK(mud.sounds[0].sound == Sound::Sludge);
  }

  TEST_CASE("the shop's lazy susan clacks softly each turn") {
    const Level level = MakeLevel(kRack);
    const std::vector<Cue> cues = RunCues(level, SoundSet::Shop);
    int clacks = 0;
    for (const Cue& cue : cues) {
      if (cue.sound != Sound::RackClack) continue;
      ++clacks;
      CHECK(cue.volume <= 0.3f);
    }
    CHECK(clacks > 0);
    CHECK(Count(cues, Sound::Click) == 0);
    CHECK(Count(RunCues(level, SoundSet::Workshop), Sound::RackClack) == 0);
  }
}

namespace {

// A run on a board already set up, tick by tick: the cues each tick asks for, and which tick each was heard on
// through a CueDelay (as Feedback plays them).
struct Heard {
  std::vector<Cue> cues;
  std::vector<int> ticks;  // parallel to cues: the tick it played on
  std::vector<int> flips;  // the ticks a gate flipped on
};
Heard HearRun(Board& board, const Level& level, SoundSet set) {
  Heard heard;
  CueDelay delay;
  board.Start();
  int tick = 0;
  while (board.Running()) {
    const std::vector<Event>& events = board.Tick();
    ++tick;
    for (const Event& e : events) {
      if (e.type == EventType::Flip) heard.flips.push_back(tick);
    }
    for (const Cue& cue : delay.Tick(CuesFor(events, level, board, set).sounds)) {
      heard.cues.push_back(cue);
      heard.ticks.push_back(tick);
    }
  }
  return heard;
}

}  // namespace

TEST_SUITE("sound cues") {
  TEST_CASE("the Canal set: vibes, a harbour served, the locks' brushes") {
    const std::vector<Cue> cues = RunCues(LanternLevel("B*4"), SoundSet::Canal);
    CHECK(Count(cues, Sound::VibesNote) == 4);
    CHECK(Count(cues, Sound::HarbourBlue) == 1);  // one sound: horn, chord, cymbal with the light
    CHECK(Count(cues, Sound::LockBrush) == 4);
    CHECK(Count(cues, Sound::NoteBlue) == 0);
    CHECK(Count(cues, Sound::ShortQuay) == 0);
  }

  TEST_CASE("a canal gate clanks a tick after its mill turns, when its leaves swing") {
    const Level level = MakeLevel(kMillGate);
    Board board(level);
    const Heard heard = HearRun(board, level, SoundSet::Canal);
    CHECK(Count(heard.cues, Sound::MillClunk) == 2);
    CHECK(Count(heard.cues, Sound::Click) == 0);
    REQUIRE(heard.flips.size() == 2);
    std::vector<int> clanks;
    for (size_t i = 0; i < heard.cues.size(); ++i) {
      if (heard.cues[i].sound == Sound::MillClunk) CHECK(heard.ticks[i] == heard.flips[clanks.size()]);
      if (heard.cues[i].sound == Sound::GateClank) clanks.push_back(heard.ticks[i]);
    }
    REQUIRE(clanks.size() == 2);
    CHECK(clanks[0] == heard.flips[0] + 1);
    CHECK(clanks[1] == heard.flips[1] + 1);

    // Elsewhere the gate clicks at once, the mill a low click.
    Board workshop(level);
    const Heard plain = HearRun(workshop, level, SoundSet::Workshop);
    CHECK(Count(plain.cues, Sound::Click) == 4);
    CHECK(Count(plain.cues, Sound::GateClank) == 0);
    for (const Cue& cue : plain.cues) CHECK(cue.ticks_later == 0);
  }

  TEST_CASE("held cues play when due; Clear drops them") {
    CueDelay delay;
    const Cue clank{.sound = Sound::GateClank, .ticks_later = 1};
    const Cue tap{.sound = Sound::Tap};
    std::vector<Cue> now = delay.Tick({clank, tap});
    REQUIRE(now.size() == 1);
    CHECK(now[0].sound == Sound::Tap);
    CHECK(delay.Waiting() == 1);
    now = delay.Tick({});
    REQUIRE(now.size() == 1);
    CHECK(now[0].sound == Sound::GateClank);
    CHECK(delay.Waiting() == 0);
    // Two ticks later.
    delay.Tick({{.sound = Sound::GateClank, .ticks_later = 2}});
    CHECK(delay.Tick({}).empty());
    CHECK(delay.Tick({}).size() == 1);
    // A run stopped while one waits: it's never heard.
    delay.Tick({clank});
    delay.Clear();
    CHECK(delay.Waiting() == 0);
    CHECK(delay.Tick({}).empty());
  }

  TEST_CASE("the Canal set: murk, the wash, a short harbour, gridlock") {
    CHECK(Count(RunCues(LanternLevel("B*5"), SoundSet::Canal), Sound::Wash) == 1);
    CHECK(Count(RunCues(LanternLevel("B*5"), SoundSet::Canal), Sound::Splash) == 0);
    const std::vector<Cue> short_run = RunCues(LanternLevel("B*3"), SoundSet::Canal, {0.0f, 100.0f});
    REQUIRE(Count(short_run, Sound::ShortQuay) == 1);
    for (const Cue& cue : short_run) {
      if (cue.sound == Sound::ShortQuay) CHECK(cue.volume == doctest::Approx(0.8f));
    }
    {
      const Level level = LanternLevel("B*4");
      Board board(level);
      board.Start();
      const Cues mud =
          CuesFor({{.type = EventType::Collide, .node = NodeOf(level, "c")}}, level, board, SoundSet::Canal);
      REQUIRE(mud.sounds.size() == 1);
      CHECK(mud.sounds[0].sound == Sound::Murk);
    }
    // 3-6 with a lock alone: THREADLOCKED. The horns over the grind, once; no short horn as well.
    const Level level = MakeLevel(kMillGateCanals);
    Board board(level);
    REQUIRE(board.Place(PieceOf(board, PieceType::Turnstile), NodeOf(level, "x")));
    const Heard heard = HearRun(board, level, SoundSet::Canal);
    CHECK(Count(heard.cues, Sound::Gridlock) == 1);
    CHECK(Count(heard.cues, Sound::Grind) == 0);
    CHECK(Count(heard.cues, Sound::ShortQuay) == 0);
  }
}

TEST_SUITE("sound cues") {
  TEST_CASE("the Sanctum set: harp, a phial full, the flasks' pours") {
    const std::vector<Cue> cues = RunCues(LanternLevel("B*4"), SoundSet::Sanctum);
    CHECK(Count(cues, Sound::HarpNote) == 4);
    CHECK(Count(cues, Sound::PhialBlue) == 1);
    CHECK(Count(cues, Sound::FlaskPour) == 4);
    CHECK(Count(cues, Sound::NoteBlue) == 0);
    CHECK(Count(cues, Sound::HollowPhial) == 0);
    CHECK(Count(cues, Sound::SealBell) == 0);
  }

  TEST_CASE("a mix sings the colour it made in the sanctum; elsewhere as before") {
    const Level level = MakeLevel(kTwinSeals);
    Board board(level);
    board.Start();
    const int m = NodeOf(level, "m");
    auto mix = [&](SoundSet set, Color made, const Listener& listener = {}) {
      const Cues cues =
          CuesFor({{.type = EventType::Mix, .node = m, .bead = Bead::Pure(made)}}, level, board, set, listener);
      REQUIRE(cues.sounds.size() == 1);
      return cues.sounds[0];
    };
    const Cue green = mix(SoundSet::Sanctum, Color::Green);
    CHECK(green.sound == Sound::MixSwell);
    CHECK(green.pitch == doctest::Approx(1.0f));
    CHECK(green.volume == doctest::Approx(0.5f));
    const Cue pearl = mix(SoundSet::Sanctum, Color::Pearl);
    CHECK(pearl.sound == Sound::MixSwell);
    CHECK(pearl.pitch == doctest::Approx(1.5f));
    const Cue clot = mix(SoundSet::Sanctum, Color::Mud, {0.0f, 100.0f});  // m is off the view: a failure, in full
    CHECK(clot.sound == Sound::Clot);
    CHECK(clot.volume == doctest::Approx(0.9f));
    const Cue workshop = mix(SoundSet::Workshop, Color::Green);
    CHECK(workshop.sound == Sound::NoteGreen);
    CHECK(workshop.volume == doctest::Approx(0.45f));
    CHECK(workshop.pitch == doctest::Approx(0.75f));
    CHECK(mix(SoundSet::Canal, Color::Mud).sound == Sound::RhodesNote);
    CHECK(mix(SoundSet::Shop, Color::Pearl).sound == Sound::NoteGreen);
    CHECK(mix(SoundSet::Lantern, Color::Green).sound == Sound::NoteGreen);
  }

  TEST_CASE("4-7: the twin seals ring once per release, the mixer sings, the phials fill") {
    const Level level = MakeLevel(kTwinSeals);
    auto ticks_of = [](const Heard& heard, Sound sound) {
      std::vector<int> ticks;
      for (size_t k = 0; k < heard.cues.size(); ++k) {
        if (heard.cues[k].sound == sound) ticks.push_back(heard.ticks[k]);
      }
      return ticks;
    };
    Board board(level);
    REQUIRE(board.Place(PieceOf(board, PieceType::TwinGate), NodeOf(level, "p1")));
    REQUIRE(board.Place(PieceOf(board, PieceType::Mixer), NodeOf(level, "m")));
    const Heard heard = HearRun(board, level, SoundSet::Sanctum);
    REQUIRE(board.GetOutcome().solved);
    CHECK(ticks_of(heard, Sound::SealBell) == std::vector<int>{18, 30, 42});
    for (const Cue& cue : heard.cues) {
      if (cue.sound == Sound::SealBell) CHECK(cue.pan == doctest::Approx(0.0f));
      if (cue.sound == Sound::MixSwell) CHECK(cue.pitch == doctest::Approx(1.0f));
    }
    CHECK(ticks_of(heard, Sound::MixSwell) == std::vector<int>{42, 54, 66});
    std::vector<float> organ;
    for (const Cue& cue : heard.cues) {
      if (cue.sound == Sound::OrganNote) organ.push_back(cue.pitch);
    }
    REQUIRE(organ.size() == 6);
    for (int k = 0; k < 6; ++k) CHECK(organ[static_cast<size_t>(k)] == doctest::Approx(synth::ClimbPitch(k)));
    CHECK(Count(heard.cues, Sound::BowlNote) == 3);
    CHECK(ticks_of(heard, Sound::PhialRose) == std::vector<int>{65});
    CHECK(ticks_of(heard, Sound::PhialGreen) == std::vector<int>{76});
    CHECK(Count(heard.cues, Sound::FlaskPour) == 9);
    CHECK(Count(heard.cues, Sound::Clot) == 0);
    CHECK(Count(heard.cues, Sound::SealCrack) == 0);
    // The same placement in the workshop: no bell.
    Board workshop(level);
    REQUIRE(workshop.Place(PieceOf(workshop, PieceType::TwinGate), NodeOf(level, "p1")));
    REQUIRE(workshop.Place(PieceOf(workshop, PieceType::Mixer), NodeOf(level, "m")));
    CHECK(Count(HearRun(workshop, level, SoundSet::Workshop).cues, Sound::SealBell) == 0);
    // The mixer alone: no seals, and the sludge at the first crossing clots.
    Board alone(level);
    REQUIRE(alone.Place(PieceOf(alone, PieceType::Mixer), NodeOf(level, "m")));
    const Heard sludge = HearRun(alone, level, SoundSet::Sanctum);
    CHECK(Count(sludge.cues, Sound::SealBell) == 0);
    CHECK(Count(sludge.cues, Sound::Clot) >= 1);
  }

  TEST_CASE("a lonely seal cracks: THREADLOCKED in the sanctum") {
    std::string json = kTwinSeals;
    json.replace(json.find("A*3"), 3, "A*2");
    const Level level = MakeLevel(json);
    Board board(level);
    REQUIRE(board.Place(PieceOf(board, PieceType::TwinGate), NodeOf(level, "p1")));
    REQUIRE(board.Place(PieceOf(board, PieceType::Mixer), NodeOf(level, "m")));
    const Heard heard = HearRun(board, level, SoundSet::Sanctum);
    CHECK(board.GetOutcome().jammed);
    CHECK(board.Jam().lonely == NodeOf(level, "p1"));
    CHECK(Count(heard.cues, Sound::SealCrack) == 1);
    CHECK(Count(heard.cues, Sound::Grind) == 0);
    CHECK(Count(heard.cues, Sound::HollowPhial) == 0);
    CHECK(Count(heard.cues, Sound::SealBell) == 2);
  }

  TEST_CASE("the Sanctum set: sludge, a spill at the brim, a short phial") {
    CHECK(Count(RunCues(LanternLevel("B*5"), SoundSet::Sanctum), Sound::PhialSpill) == 1);
    CHECK(Count(RunCues(LanternLevel("B*5"), SoundSet::Sanctum), Sound::Splash) == 0);
    const std::vector<Cue> short_run = RunCues(LanternLevel("B*3"), SoundSet::Sanctum, {0.0f, 100.0f});
    REQUIRE(Count(short_run, Sound::HollowPhial) == 1);
    for (const Cue& cue : short_run) {
      if (cue.sound == Sound::HollowPhial) CHECK(cue.volume == doctest::Approx(0.8f));
    }
    const Level level = LanternLevel("B*4");
    Board board(level);
    board.Start();
    const Cues mud =
        CuesFor({{.type = EventType::Collide, .node = NodeOf(level, "c")}}, level, board, SoundSet::Sanctum);
    REQUIRE(mud.sounds.size() == 1);
    CHECK(mud.sounds[0].sound == Sound::Clot);
  }
}
