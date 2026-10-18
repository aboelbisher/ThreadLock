#include "map_screen.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string>

#include "audio_synth.h"
#include "ui_parts.h"
#include "wlw/core/cvar.h"
#include "wlw/ui/icons.h"
#include "wlw/ui/widgets.h"

namespace threadlock {
namespace {

using wlw::ui::Align;
using wlw::ui::Icon;
using wlw::ui::Rect;
using wlw::ui::UIContext;

// Replays the map's animations (adb: am start ... --es wlw "tl.map=seen:1-3"): "seen:<level>" = the map
// last saw World 1 up to that level, "seen:none" = never (the first visit), "gate:<world>" = everything
// but that world's gate opening. Used once, by the first map opened.
wlw::CVar<std::string> cv_map{"tl.map", "", "Replay the map's animations: seen:<level>|seen:none|gate:<world> (dev)"};

constexpr float kBar = 58.0f;     // the top bar under the safe area's top
constexpr float kRound = 64.0f;   // Daily, Lab
constexpr float kAppear = 0.45f;  // seconds for a star or a pane to light
constexpr float kFlash = 0.7f;
constexpr int kAnimated = 3;  // at most this many levels light one by one; earlier news shows at once

constexpr Rgba kHudFill = Rgba{20.0f / 255.0f, 28.0f / 255.0f, 44.0f / 255.0f, 0.9f};
constexpr Rgba kHudBorder = Rgba{207.0f / 255.0f, 230.0f / 255.0f, 1.0f, 0.2f};
constexpr Rgba kBrassLit = Hex(0xF6DA9A);
constexpr float kText = 1.15f;  // CSS font sizes -> canvas line heights

float Clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }
float EaseInOut(float k) { return k < 0.5f ? 2.0f * k * k : 1.0f - std::pow(-2.0f * k + 2.0f, 2.0f) / 2.0f; }
bool Inside(const Rect& r, glm::vec2 p) { return p.x >= r.min.x && p.x <= r.max.x && p.y >= r.min.y && p.y <= r.max.y; }

void Pill(wlw::ui::Canvas& c, const Rect& r, const Rgba& fill, const Rgba& border) {
  const float corner = r.Size().y * 0.5f;
  c.FillRect(r, border, corner);
  c.FillRect({r.min + glm::vec2(1.0f), r.max - glm::vec2(1.0f)}, fill, corner - 1.0f);
}

std::string Upper(std::string text) {
  for (char& ch : text) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
  return text;
}

}  // namespace

MapScreen::MapScreen(ThreadlockApp& app, const Level* focus) : app_(app), focus_(focus) {}

void MapScreen::OnEnter() {
  app_.GetLiquid().SetActive(false);
  app_.GetFeedback().SetAmbience(true, true);  // World 1's room, softer
  Build();
}

void MapScreen::OnExit() {
  app_.GetFeedback().SetAmbience(false, false);
  app_.GetCityArt().Release();
}

void MapScreen::Build() {
  const Catalog& catalog = app_.GetCatalog();
  levels_.clear();
  for (const World& world : catalog.Worlds()) {
    city::WorldLevels w;
    w.world = world.number;
    w.gate = Catalog::Gate(world.number);
    for (const Level* level : catalog.MainPath(world.number)) w.main.push_back(level->id);
    for (const Level* level : catalog.Challenges(world.number)) w.challenges.push_back(level->id);
    levels_.push_back(std::move(w));
  }
  auto place = [&](int world, auto& slots, int main_count) {
    const std::vector<const Level*> path = catalog.MainPath(world), challenges = catalog.Challenges(world);
    for (size_t i = 0; i < path.size() && i < static_cast<size_t>(main_count); ++i) slots[i] = path[i];
    for (size_t i = 0; i < challenges.size() && main_count + i < slots.size(); ++i)
      slots[main_count + i] = challenges[i];
  };
  place(1, lanterns_, city::kPetals);
  for (int w = 2; w <= 4; ++w) place(w, lamps_[static_cast<size_t>(w - 2)], city::kDistrictLamps - city::kLobes);
  for (int i = 0; i < city::kLanterns; ++i) {
    if (lanterns_[static_cast<size_t>(i)]) frame_.colors[static_cast<size_t>(i)] = city::LanternColors(*lanterns_[i]);
  }
  if (const World* first = catalog.GetWorld(1)) frame_.tower = Upper(first->name);
  for (int w = 2; w <= 4; ++w) {
    CityFrame::District& d = frame_.districts[static_cast<size_t>(w - 2)];
    if (const World* world = catalog.GetWorld(w)) d.name = world->name;
    d.gate = Catalog::Gate(w);
  }
}

// ---- The news ----

void MapScreen::Start(float view) {
  Progress& progress = app_.GetProgress();
  const StarMap& now = progress.Stars();
  city::Seen seen = progress.HasMapSeen() ? progress.MapSeen() : city::SeedSeen(levels_, now);
  if (const std::string dev = cv_map.Get(); !dev.empty()) {
    cv_map.Set("");
    if (dev == "seen:none") {
      seen = {};
    } else if (dev.rfind("seen:", 0) == 0) {
      seen = {};
      const city::WorldLevels& first = levels_.front();
      for (const std::string& id : first.main) {
        if (const auto it = now.find(id); it != now.end()) seen.stars[id] = it->second;
        if (id == dev.substr(5)) break;
      }
    } else if (dev.rfind("gate:", 0) == 0) {
      seen = city::SeeAll(levels_, now);
      seen.gates = std::max(1, std::atoi(dev.c_str() + 5) - 1);
    }
  }
  city::News news = city::WhatsNew(levels_, seen, now);
  progress.SetMapSeen(city::SeeAll(levels_, now));

  // What was seen is lit from the start.
  for (auto& lantern : lantern_at_) lantern.fill(kNever);
  for (auto& pane : pane_at_) pane.fill(kNever);
  for (auto& lamps : lamp_at_) lamps.fill(kNever);
  stars_shown_ = 0;
  for (const city::WorldLevels& w : levels_) {
    auto lit = [&](const std::string& id, int index) {
      const auto it = seen.stars.find(id);
      const int bits = it == seen.stars.end() ? 0 : it->second;
      stars_shown_ += StarsIn(bits);
      for (int k = 0; k < 3; ++k) {
        if (!(bits & (1 << k))) continue;
        if (w.world == 1 && index < city::kLanterns) {
          lantern_at_[static_cast<size_t>(index)][static_cast<size_t>(k)] = kLongAgo;
          pane_at_[static_cast<size_t>(index)][static_cast<size_t>(k)] = kLongAgo;
        } else if (w.world >= 2 && w.world <= 4 && index < city::kDistrictLamps && k == 0) {
          lamp_at_[static_cast<size_t>(w.world - 2)][static_cast<size_t>(index)] = kLongAgo;
        }
      }
    };
    for (size_t i = 0; i < w.main.size(); ++i) lit(w.main[i], static_cast<int>(i));
    const int first_challenge = w.world == 1 ? city::kPetals : city::kDistrictLamps - city::kLobes;
    for (size_t i = 0; i < w.challenges.size(); ++i) lit(w.challenges[i], first_challenge + static_cast<int>(i));
  }
  for (int w = 2; w <= 4; ++w) gate_shown_[static_cast<size_t>(w - 2)] = seen.gates >= w;

  const Level* current = progress.Current(app_.GetCatalog());
  const Level* target = focus_ ? focus_ : current;
  if (news.Empty()) {
    SnapTo(LanternY(target) - view * (560.0f / city::kMockView));
    return;
  }

  // Earlier news lights at once; the last few play one by one.
  if (news.reveals.size() > static_cast<size_t>(kAnimated)) {
    const size_t quiet = news.reveals.size() - kAnimated;
    for (size_t r = 0; r < quiet; ++r) {
      const city::Reveal& reveal = news.reveals[r];
      for (int k = 0; k < 3; ++k) {
        if (!(reveal.fresh & (1 << k))) continue;
        ++stars_shown_;
        if (reveal.world == 1) {
          lantern_at_[static_cast<size_t>(reveal.index)][static_cast<size_t>(k)] = kLongAgo;
          pane_at_[static_cast<size_t>(reveal.index)][static_cast<size_t>(k)] = kLongAgo;
        } else if (k == 0) {
          lamp_at_[static_cast<size_t>(reveal.world - 2)][static_cast<size_t>(reveal.index)] = kLongAgo;
        }
      }
    }
    news.reveals.erase(news.reveals.begin(), news.reveals.begin() + static_cast<long>(quiet));
  }

  // Where the view starts: the window on the first visit, else the first news.
  float planned = 0.0f;
  auto scroll_for = [&](const city::Reveal& r) {
    if (r.world == 1) return city::LanternScroll(r.index, view);
    const Vec2 lamp = city::DistrictLamps(r.world)[static_cast<size_t>(r.index)];
    return city::ClampScroll(lamp.y - view * (560.0f / city::kMockView), view);
  };
  if (seen.stars.empty()) {
    planned = city::WindowScroll(view);
  } else if (!news.reveals.empty()) {
    planned = scroll_for(news.reveals.front());
  } else {
    planned = city::GateScroll(news.gates.front(), view);
  }
  SnapTo(planned);

  float t = 0.6f;  // after the fade in
  auto glide = [&](float to, float seconds, float after) {
    to = city::ClampScroll(to, view);
    if (std::fabs(to - planned) < 4.0f) return;
    At(t, [this, to, seconds] { Glide(to, seconds); });
    t += seconds + after;
    planned = to;
  };
  // World 1 first; then, district by district, its gate opening (if it just opened) and its lamps.
  for (int world = 1; world <= 4; ++world) {
    if (std::find(news.gates.begin(), news.gates.end(), world) != news.gates.end()) {
      glide(city::GateScroll(world, view), 1.0f, 0.3f);
      t = OpenGate(world, t);
      planned = -1.0f;  // the pour moved the view
    }
    for (const city::Reveal& r : news.reveals) {
      if (r.world != world) continue;
      if (r.world == 1) {
        const float lantern = city::Lanterns()[static_cast<size_t>(r.index)].at.y;
        if (lantern < planned + view * 0.15f || lantern > planned + view * 0.8f) glide(scroll_for(r), 0.7f, 0.2f);
        t = LightLantern(r.index, r.fresh, t);
        if (!city::WindowInView(planned, view)) glide(city::WindowScroll(view), 0.9f, 0.15f);
        t = LightPanes(r.index, r.fresh, t) + 0.2f;
      } else {
        glide(scroll_for(r), 0.8f, 0.2f);
        t = LightLamp(r.world, r.index, t);
      }
    }
  }
  // Then the next level to play.
  const float y = LanternY(current);
  const int next = [&] {
    for (int i = 0; i < city::kLanterns; ++i) {
      if (lanterns_[static_cast<size_t>(i)] == current) return i;
    }
    return -1;
  }();
  if (y >= 0.0f) {
    const float before = t;
    glide(y - view * (560.0f / city::kMockView), 0.9f, 0.05f);
    if (t == before) t += 0.3f;
  }
  if (next >= 0) {
    At(t, [this, next] {
      pulse_ = next;
      pulse_until_ = clock_ + 1.8f;
    });
  }
  busy_until_ = t + 0.1f;
}

float MapScreen::Chime() { return synth::ClimbPitch(note_++ % 8); }

// The lantern lights with its first star, then its Lean and Swift stars pop.
float MapScreen::LightLantern(int lantern, int fresh, float start) {
  float t = start;
  for (int k = 0; k < 3; ++k) {
    if (!(fresh & (1 << k))) continue;
    At(t, [this, lantern, k] {
      lantern_at_[static_cast<size_t>(lantern)][static_cast<size_t>(k)] = clock_;
      ++stars_shown_;
      app_.GetFeedback().Play(synth::Sound::PaneChime, 0.55f, Chime());
    });
    t += 0.32f;
  }
  return t + 0.25f;
}

// Its petal: the big pane, then Lean, then Swift (a challenge lights its lobe of the heart).
float MapScreen::LightPanes(int lantern, int fresh, float start) {
  float t = start;
  const bool petal = lantern < city::kPetals;
  for (int k = 0; k < 3; ++k) {
    if (!(fresh & (1 << k)) || (!petal && k > 0)) continue;
    At(t, [this, lantern, k, petal, fresh] {
      auto& pane = pane_at_[static_cast<size_t>(lantern)];
      pane[static_cast<size_t>(k)] = clock_;
      if (!petal) {  // a lobe shows the challenge's other stars with it
        for (int j = 1; j < 3; ++j) {
          if (fresh & (1 << j)) pane[static_cast<size_t>(j)] = clock_;
        }
      }
      app_.GetFeedback().Play(synth::Sound::PaneChime, 0.5f, Chime() * 0.5f);
    });
    t += 0.48f;
  }
  return t;
}

float MapScreen::LightLamp(int world, int lamp, float start) {
  const Level* level = lamps_[static_cast<size_t>(world - 2)][static_cast<size_t>(lamp)];
  const int stars = level ? app_.GetProgress().StarCount(level->id) : 1;
  At(start, [this, world, lamp, stars] {
    lamp_at_[static_cast<size_t>(world - 2)][static_cast<size_t>(lamp)] = clock_;
    stars_shown_ += stars;
    app_.GetFeedback().Play(synth::Sound::PaneChime, 0.55f, Chime());
  });
  return start + 0.7f;
}

// The valve turns, light pours down the pipe (the view rides with it), the first lamp wakes.
float MapScreen::OpenGate(int world, float start) {
  const size_t d = static_cast<size_t>(world - 2);
  float t = start;
  At(t, [this, d] {
    valve_at_[d] = clock_;
    app_.GetFeedback().Play(synth::Sound::Grind, 0.35f, 1.6f);
  });
  t += 1.0f;
  At(t, [this, d, world] {
    pour_at_[d] = clock_;
    app_.GetFeedback().Play(synth::Sound::Pour, 0.6f, 0.8f);
    const city::Cubic pipe = city::GatePipe(world);
    const float t0 = clock_;
    glide_.on = true;
    glide_.follow = [this, pipe, t0](float now) {
      return pipe.At(Clamp01((now - t0) / 1.9f)).y - view_ * (430.0f / city::kMockView);
    };
  });
  t += 1.9f;
  At(t, [this, d] {
    wake_at_[d] = clock_;
    gate_shown_[d] = true;
    glide_ = {};
    app_.GetFeedback().Play(synth::Sound::Chime, 0.7f, 1.0f);
  });
  return t + 0.9f;
}

void MapScreen::At(float delay, std::function<void()> run) { events_.push_back({clock_ + delay, std::move(run)}); }

void MapScreen::SnapTo(float scroll) {
  glide_ = {};
  camera_.Snap(scroll);
}

void MapScreen::Glide(float to, float seconds) {
  glide_ = {};
  glide_.on = true;
  glide_.from = camera_.Y();
  glide_.to = to;
  glide_.t0 = clock_;
  glide_.seconds = std::max(0.05f, seconds);
}

void MapScreen::ShowAll() {
  const StarMap& now = app_.GetProgress().Stars();
  stars_shown_ = 0;
  for (const city::WorldLevels& w : levels_) {
    auto show = [&](const std::string& id, int index) {
      const auto it = now.find(id);
      const int bits = it == now.end() ? 0 : it->second;
      stars_shown_ += StarsIn(bits);
      for (int k = 0; k < 3; ++k) {
        if (!(bits & (1 << k))) continue;
        if (w.world == 1 && index < city::kLanterns) {
          float& lantern = lantern_at_[static_cast<size_t>(index)][static_cast<size_t>(k)];
          float& pane = pane_at_[static_cast<size_t>(index)][static_cast<size_t>(k)];
          lantern = std::min(lantern, kLongAgo);
          pane = std::min(pane, kLongAgo);
        } else if (w.world >= 2 && w.world <= 4 && index < city::kDistrictLamps && k == 0) {
          float& lamp = lamp_at_[static_cast<size_t>(w.world - 2)][static_cast<size_t>(index)];
          lamp = std::min(lamp, kLongAgo);
        }
      }
    };
    for (size_t i = 0; i < w.main.size(); ++i) show(w.main[i], static_cast<int>(i));
    const int first_challenge = w.world == 1 ? city::kPetals : city::kDistrictLamps - city::kLobes;
    for (size_t i = 0; i < w.challenges.size(); ++i) show(w.challenges[i], first_challenge + static_cast<int>(i));
  }
  const int open = city::OpenGates(levels_, now);
  for (int w = 2; w <= 4; ++w)
    gate_shown_[static_cast<size_t>(w - 2)] = gate_shown_[static_cast<size_t>(w - 2)] || open >= w;
}

void MapScreen::Skip() {
  events_.clear();
  busy_until_ = 0.0f;
  pulse_ = -1;
  ShowAll();
  const Level* current = app_.GetProgress().Current(app_.GetCatalog());
  SnapTo(LanternY(current) - view_ * (560.0f / city::kMockView));
}

// ---- Frames ----

void MapScreen::Update(float dt) {
  clock_ += dt;
  // Events that are due (one may add none: they only set state).
  std::vector<Event> due;
  for (auto it = events_.begin(); it != events_.end();) {
    if (it->at <= clock_) {
      due.push_back(std::move(*it));
      it = events_.erase(it);
    } else {
      ++it;
    }
  }
  std::stable_sort(due.begin(), due.end(), [](const Event& a, const Event& b) { return a.at < b.at; });
  for (Event& event : due) event.run();

  const float before = camera_.Y();
  if (glide_.on) {
    float y = 0.0f;
    if (glide_.follow) {
      y = glide_.follow(clock_);
    } else {
      const float k = Clamp01((clock_ - glide_.t0) / glide_.seconds);
      y = glide_.from + (glide_.to - glide_.from) * EaseInOut(k);
      if (k >= 1.0f) glide_.on = false;
    }
    camera_.Snap(y);
  } else {
    camera_.Update(dt);
  }
  const float moved = camera_.Y() - before;
  if (std::fabs(moved) > 0.01f) heading_ = moved;
}

bool MapScreen::LockedShown(int lantern) const {
  // Locked as drawn: the lantern before it hasn't lit yet (so a solve unlocks the next one as it lights).
  auto shown = [&](int i) { return lantern_at_[static_cast<size_t>(i)][0] <= clock_; };
  if (shown(lantern)) return false;  // lit is never locked
  if (lantern < city::kPetals) return lantern > 0 && !shown(lantern - 1);
  return !shown(city::kPetals - 1);
}

float MapScreen::LanternY(const Level* level) const {
  if (!level) return -1.0f;
  for (int i = 0; i < city::kLanterns; ++i) {
    if (lanterns_[static_cast<size_t>(i)] == level) return city::Lanterns()[static_cast<size_t>(i)].at.y;
  }
  for (int w = 2; w <= 4; ++w) {
    const auto lamps = city::DistrictLamps(w);
    for (int j = 0; j < city::kDistrictLamps; ++j) {
      if (lamps_[static_cast<size_t>(w - 2)][static_cast<size_t>(j)] == level) return lamps[static_cast<size_t>(j)].y;
    }
  }
  return -1.0f;
}

void MapScreen::Fill(CityFrame& frame, glm::vec2 screen, float scale) {
  const Progress& progress = app_.GetProgress();
  const StarMap& now = progress.Stars();
  frame.scale = scale;
  frame.scroll = camera_.Y();
  frame.screen = screen;
  frame.calm = app_.Calm();
  frame.time = frame.calm ? 0.0f : app_.Time();
  frame.palette = &app_.GetPalette();
  frame.font = &app_.GetFont();
  frame.stars = stars_shown_;
  auto alpha = [&](float at) { return Clamp01((clock_ - at) / kAppear); };
  auto flash = [&](float at) {
    if (!std::isfinite(at) || at < 0.0f || clock_ < at) return 0.0f;
    return Clamp01(1.0f - (clock_ - at) / kFlash) * 0.85f;
  };
  int tower_stars = 0;
  for (int i = 0; i < city::kLanterns; ++i) {
    const size_t n = static_cast<size_t>(i);
    frame.locked[n] = LockedShown(i) || !lanterns_[n];
    for (size_t k = 0; k < 3; ++k) {
      frame.lantern[n][k] = alpha(lantern_at_[n][k]);
      frame.lantern_flash[n][k] = flash(lantern_at_[n][k]);
      frame.pane[n][k] = alpha(pane_at_[n][k]);
      frame.pane_flash[n][k] = flash(pane_at_[n][k]);
      if (i < city::kPetals && lantern_at_[n][k] <= clock_) ++tower_stars;
    }
  }
  const Level* current = progress.Current(app_.GetCatalog());
  frame.breathe = -1;
  frame.pulse = false;
  if (pulse_ >= 0 && clock_ < pulse_until_) {
    frame.breathe = pulse_;
    frame.pulse = true;
  } else if (!Busy()) {
    for (int i = 0; i < city::kLanterns; ++i) {
      if (lanterns_[static_cast<size_t>(i)] == current && !progress.Solved(current->id)) frame.breathe = i;
    }
  }
  frame.tower_fill = city::PipeFill(tower_stars, Catalog::Gate(2));
  for (int w = 2; w <= 4; ++w) {
    const size_t d = static_cast<size_t>(w - 2);
    CityFrame::District& df = frame.districts[d];
    df.open = gate_shown_[d];
    df.have = city::StarsBefore(levels_, now, w);
    df.valve = valve_at_[d] > 0.0f ? Clamp01((clock_ - valve_at_[d]) / 0.9f) : 0.0f;
    df.pour = df.open ? 1.0f : pour_at_[d] > 0.0f ? Clamp01((clock_ - pour_at_[d]) / 1.9f) : 0.0f;
    df.wake = wake_at_[d] > 0.0f ? Clamp01((clock_ - wake_at_[d]) / 0.6f) : 0.0f;
    const auto lights = city::DistrictLights(levels_, now, w);
    df.current = -1;
    for (int j = 0; j < city::kDistrictLamps; ++j) {
      city::Light& lamp = df.lamps[static_cast<size_t>(j)];
      lamp.locked = !df.open || lights[static_cast<size_t>(j)].locked || !lamps_[d][static_cast<size_t>(j)];
      lamp.bits = lamp_at_[d][static_cast<size_t>(j)] <= clock_ ? kSolveStar : 0;
      if (lamps_[d][static_cast<size_t>(j)] == current && current && !progress.Solved(current->id) && !Busy())
        df.current = j;
    }
  }
}

// ---- Input ----

void MapScreen::Tap(glm::vec2 map) {
  ThreadlockApp& app = app_;
  const Progress& progress = app.GetProgress();
  const Catalog& catalog = app.GetCatalog();
  auto open = [&](const Level* level, const std::string& locked_text) {
    if (!level) return;
    if (progress.LevelOpen(catalog, *level)) {
      app.GetFeedback().Play(synth::Sound::Tap);
      app.OpenLevel(level);
    } else {
      app.GetFeedback().Play(synth::Sound::Click, 0.6f, 0.7f);
      app.Toast(locked_text);
    }
  };
  if (const int i = city::HitLantern({map.x, map.y}); i >= 0) {
    const Level* level = lanterns_[static_cast<size_t>(i)];
    const Level* before = i > 0 && i < city::kPetals ? lanterns_[static_cast<size_t>(i - 1)] : nullptr;
    open(level, i < city::kPetals ? (before ? "Solve " + before->id + " to open it" : "")
                                  : "Finish 1-15 to open the side path");
    return;
  }
  for (int w = 2; w <= 4; ++w) {
    const city::District& d = city::Districts()[static_cast<size_t>(w - 1)];
    const int need = Catalog::Gate(w) - city::StarsBefore(levels_, progress.Stars(), w);
    const std::string gate_text = std::to_string(std::max(1, need)) + " more stars open " + d.place;
    if (const int j = city::HitLamp(w, {map.x, map.y}); j >= 0) {
      open(lamps_[static_cast<size_t>(w - 2)][static_cast<size_t>(j)],
           progress.WorldOpen(catalog, w)
               ? (j >= 15 ? "Finish the district to open its challenges" : "Solve the levels before it first")
               : gate_text);
      return;
    }
    if (std::hypot(map.x - 195.0f, map.y - d.valve) < 34.0f && !progress.WorldOpen(catalog, w)) {
      app.GetFeedback().Play(synth::Sound::Click, 0.6f, 0.7f);
      app.Toast(gate_text);
      return;
    }
  }
}

bool MapScreen::Button(UIContext& ui, const Rect& rect) {
  // Pressed and released inside, by a finger that didn't drag the map.
  return ui.interactive && ui.pointer.released && Inside(rect, ui.pointer.position) && Inside(rect, press_at_);
}

void MapScreen::RenderUI(UIContext& ui) {
  wlw::ui::Canvas& canvas = ui.canvas;
  const glm::vec2 screen = canvas.Size();
  scale_ = screen.x / city::kWidth;
  view_ = screen.y / scale_;
  camera_.SetView(view_, city::kHeight);
  if (!started_) {
    started_ = true;
    Start(view_);
  }

  // The finger: a drag scrolls (and flings), a tap picks a lantern; any touch skips an animation.
  const Rect safe = ui.safe_area;
  const glm::vec2 p = ui.pointer.position;
  const Rect daily{{safe.min.x + 14.0f, safe.max.y - 16.0f - kRound},
                   {safe.min.x + 14.0f + kRound, safe.max.y - 16.0f}};
  const Rect lab{{safe.max.x - 14.0f - kRound, safe.max.y - 16.0f - kRound}, {safe.max.x - 14.0f, safe.max.y - 16.0f}};
  if (ui.pointer.pressed) press_at_ = p;
  if (ui.pointer.pressed && ui.interactive) {
    const bool hud = p.y < safe.min.y + kBar || Inside(daily, p) || Inside(lab, p);
    if (Busy() || (glide_.on && glide_.follow)) {
      Skip();
    } else if (!hud) {
      pressed_ = true;
      moved_ = false;
      last_y_ = p.y;
      velocity_ = 0.0f;
      glide_ = {};
      camera_.BeginDrag();
    }
  }
  if (pressed_) {
    if (glm::distance(p, press_at_) > 10.0f) moved_ = true;
    if (moved_) {
      const float dy = (last_y_ - p.y) / scale_;
      camera_.Drag(dy);
      if (ui.dt > 0.0f) velocity_ = velocity_ * 0.6f + 0.4f * dy / ui.dt;
    }
    last_y_ = p.y;
    if (ui.pointer.released || !ui.pointer.down) {
      pressed_ = false;
      camera_.EndDrag(moved_ ? velocity_ : 0.0f);
      if (!moved_ && ui.interactive) Tap({p.x / scale_, p.y / scale_ + camera_.Y()});
    }
  }

  Fill(frame_, screen, scale_);
  app_.GetCityArt().Draw(app_.GetLiquid().Picture(), canvas, frame_, heading_);
  DrawHud(ui);
}

void MapScreen::DrawHud(UIContext& ui) {
  wlw::ui::Canvas& canvas = ui.canvas;
  const wlw::ui::Font& font = app_.GetFont();
  Progress& progress = app_.GetProgress();
  const Catalog& catalog = app_.GetCatalog();
  const Rect safe = ui.safe_area;
  const float width = canvas.Size().x;

  // The slim top bar: stars, hints; trophies and settings.
  const float top = safe.min.y, bottom = top + kBar, mid = top + kBar * 0.5f;
  for (int k = 0; k < 8; ++k) {
    const float y0 = bottom * static_cast<float>(k) / 8.0f, y1 = bottom * static_cast<float>(k + 1) / 8.0f;
    canvas.FillRect({{0.0f, y0}, {width, y1}}, Hex(0x060910, 0.94f - 0.39f * static_cast<float>(k) / 7.0f));
  }
  canvas.FillRect({{0.0f, bottom - 1.0f}, {width, bottom}}, WithAlpha(kBrassLit, 0.14f));
  const float size = 18.0f * kText;
  const std::string stars = std::to_string(progress.TotalStars()), hints = std::to_string(progress.Hints());
  float x = safe.min.x + 12.0f;
  const float stars_w = 12.0f + 18.0f + 6.0f + font.Measure(stars, size) + 12.0f;
  Pill(canvas, {{x, mid - 17.0f}, {x + stars_w, mid + 17.0f}}, kHudFill, WithAlpha(kBrassLit, 0.3f));
  StarShape(canvas, {x + 21.0f, mid}, 9.5f, Hex(0xFFC53D));
  canvas.Text(font, stars, {x + 36.0f, mid}, size, Align::Left, {.color = Hex(0xF2F7FF)});
  x += stars_w + 8.0f;
  const float hints_w = 12.0f + 18.0f + 5.0f + font.Measure(hints, size) + 12.0f;
  Pill(canvas, {{x, mid - 17.0f}, {x + hints_w, mid + 17.0f}}, kHudFill, kHudBorder);
  wlw::ui::DrawIcon(canvas, Icon::Hint, {{x + 12.0f, mid - 9.0f}, {x + 30.0f, mid + 9.0f}}, Hex(0xE6F4FF));
  canvas.Text(font, hints, {x + 35.0f, mid}, size, Align::Left, {.color = Hex(0xF2F7FF)});

  const float right = safe.max.x - 12.0f;
  const Rect settings{{right - 44.0f, mid - 22.0f}, {right, mid + 22.0f}};
  const Rect trophies{{right - 96.0f, mid - 22.0f}, {right - 52.0f, mid + 22.0f}};
  for (const Rect& r : {settings, trophies}) Pill(canvas, r, kHudFill, kHudBorder);
  wlw::ui::DrawIcon(canvas, Icon::Settings, {settings.min + glm::vec2(11.0f), settings.max - glm::vec2(11.0f)},
                    Hex(0xE6F4FF));
  wlw::ui::DrawIcon(canvas, Icon::Trophy, {trophies.min + glm::vec2(11.0f), trophies.max - glm::vec2(11.0f)},
                    kBrassLit);
  if (Button(ui, settings)) {
    app_.GetFeedback().Play(synth::Sound::Tap);
    app_.OpenSettings();
  }
  if (Button(ui, trophies)) {
    app_.GetFeedback().Play(synth::Sound::Tap);
    app_.OpenAchievements();
  }

  // Daily and the Lab in the bottom corners.
  const int today = app_.Today();
  const bool daily_done = progress.Daily().Marked(today);
  const bool lab_open = progress.LabOpen(catalog);
  auto round_button = [&](const Rect& r, Icon icon, const char* label, bool dim, bool dot) {
    const glm::vec2 c = r.Center();
    canvas.Circle(c + glm::vec2(0.0f, 4.0f), kRound * 0.5f + 3.0f, Rgba{0.0f, 0.0f, 0.0f, 0.35f});
    canvas.Circle(c, kRound * 0.5f, Hex(0xC9974E, dim ? 0.5f : 1.0f));
    canvas.Circle(c, kRound * 0.5f - 2.0f, Hex(0x121A2A));
    canvas.Circle(c + glm::vec2(0.0f, -6.0f), kRound * 0.32f, Hex(0x22304A, 0.7f));
    const Rgba ink = WithAlpha(kBrassLit, dim ? 0.5f : 1.0f);
    wlw::ui::DrawIcon(canvas, icon, {c + glm::vec2(-12.0f, -19.0f), c + glm::vec2(12.0f, 5.0f)}, ink);
    canvas.Text(font, label, c + glm::vec2(0.0f, 16.0f), 11.0f * kText, Align::Center, {.color = ink});
    if (dot) canvas.Circle(c + glm::vec2(22.0f, -22.0f), 6.0f, Hex(0xFFC53D));
  };
  round_button({{safe.min.x + 14.0f, safe.max.y - 16.0f - kRound}, {safe.min.x + 14.0f + kRound, safe.max.y - 16.0f}},
               daily_done ? Icon::Check : Icon::Clock, "Daily", false, !daily_done);
  round_button({{safe.max.x - 14.0f - kRound, safe.max.y - 16.0f - kRound}, {safe.max.x - 14.0f, safe.max.y - 16.0f}},
               lab_open ? Icon::Grid : Icon::Lock, "Lab", !lab_open, false);
  if (Button(ui,
             {{safe.min.x + 14.0f, safe.max.y - 16.0f - kRound}, {safe.min.x + 14.0f + kRound, safe.max.y - 16.0f}})) {
    app_.GetFeedback().Play(synth::Sound::Tap);
    app_.OpenDailyMenu();
  }
  if (Button(ui,
             {{safe.max.x - 14.0f - kRound, safe.max.y - 16.0f - kRound}, {safe.max.x - 14.0f, safe.max.y - 16.0f}})) {
    app_.GetFeedback().Play(synth::Sound::Tap);
    if (lab_open) {
      app_.OpenLabMenu();
    } else {
      app_.Toast("Finish World 2 to open the Lab");
    }
  }
}

}  // namespace threadlock
