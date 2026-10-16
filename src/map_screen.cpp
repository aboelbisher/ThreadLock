#include "map_screen.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "ui_parts.h"
#include "wlw/ui/icons.h"
#include "wlw/ui/widgets.h"

namespace threadlock {
namespace {

using wlw::ui::Align;
using wlw::ui::Icon;
using wlw::ui::Rect;
using wlw::ui::UIContext;
using wlw::ui::Variant;

constexpr float kRow = 86.0f;
constexpr float kNode = 25.0f;

}  // namespace

MapScreen::MapScreen(ThreadlockApp& app, const Level* focus) : app_(app), focus_(focus) {}

void MapScreen::OnEnter() { app_.GetLiquid().SetActive(false); }

void MapScreen::Build(float width) {
  spots_.clear();
  headers_.clear();
  const float xs[3] = {width * 0.2f, width * 0.5f, width * 0.8f};
  float y = 16.0f;
  const Catalog& catalog = app_.GetCatalog();
  for (const World& world : catalog.Worlds()) {
    headers_.push_back({world.number, y});
    y += 76.0f;
    const std::vector<const Level*> path = catalog.MainPath(world.number);
    for (size_t i = 0; i < path.size(); ++i) {
      const size_t row = i / 3;
      size_t col = i % 3;
      if (row % 2 == 1) col = 2 - col;
      spots_.push_back({path[i], {xs[col], y + static_cast<float>(row) * kRow + 28.0f}, false});
    }
    y += static_cast<float>((path.size() + 2) / 3) * kRow + 10.0f;
    const std::vector<const Level*> challenges = catalog.Challenges(world.number);
    if (!challenges.empty()) {
      y += 26.0f;
      for (size_t i = 0; i < challenges.size() && i < 3; ++i)
        spots_.push_back({challenges[i], {xs[i], y + 28.0f}, true});
      y += kRow;
    }
    y += 14.0f;
  }
  content_height_ = y + 30.0f;
  built_width_ = width;
}

void MapScreen::Update(float dt) {
  if (!pressed_) {
    scroll_ += velocity_ * dt;
    velocity_ *= std::exp(-4.0f * dt);
    if (std::fabs(velocity_) < 5.0f) velocity_ = 0.0f;
  }
}

void MapScreen::RenderUI(UIContext& ui) {
  wlw::ui::Canvas& canvas = ui.canvas;
  const wlw::ui::Font& font = app_.GetFont();
  Progress& progress = app_.GetProgress();
  const Catalog& catalog = app_.GetCatalog();
  const Rect safe = ui.safe_area;
  const float width = safe.Size().x;
  if (std::fabs(built_width_ - width) > 0.5f) Build(width);
  view_top_ = safe.min.y + 186.0f;
  view_bottom_ = safe.max.y;
  const float visible = view_bottom_ - view_top_;
  const float max_scroll = std::max(0.0f, content_height_ - visible);

  if (!focused_) {
    focused_ = true;
    const Level* target = focus_ ? focus_ : progress.Current(catalog);
    for (const Spot& spot : spots_) {
      if (spot.level == target) scroll_ = spot.at.y - visible * 0.45f;
    }
  }

  // Scrolling and taps.
  const glm::vec2 p = ui.pointer.position;
  if (ui.pointer.pressed && p.y > view_top_ && ui.interactive) {
    pressed_ = true;
    moved_ = false;
    press_at_ = p;
    press_scroll_ = scroll_;
    last_y_ = p.y;
    velocity_ = 0.0f;
  }
  if (pressed_) {
    if (std::fabs(p.y - press_at_.y) > 10.0f) moved_ = true;
    if (moved_) {
      scroll_ = press_scroll_ + (press_at_.y - p.y);
      if (ui.dt > 0.0f) velocity_ = velocity_ * 0.6f + 0.4f * (last_y_ - p.y) / ui.dt;
    }
    last_y_ = p.y;
    if (ui.pointer.released || !ui.pointer.down) {
      pressed_ = false;
      if (!moved_ && ui.interactive) {
        velocity_ = 0.0f;
        for (const Spot& spot : spots_) {
          const glm::vec2 at{safe.min.x + spot.at.x, view_top_ + spot.at.y - scroll_};
          if (glm::distance(at, p) > kNode + 10.0f) continue;
          if (progress.LevelOpen(catalog, *spot.level)) {
            app_.GetFeedback().Play(synth::Sound::Tap);
            app_.OpenLevel(spot.level);
          } else {
            app_.GetFeedback().Play(synth::Sound::Click, 0.6f, 0.7f);
            app_.Toast(spot.challenge ? "Finish the world to open its challenges" : "Solve the levels before it first");
          }
          break;
        }
      }
    }
  }
  scroll_ = std::clamp(scroll_, 0.0f, max_scroll);

  DrawWorld(ui, app_.GetLiquid().Picture(), view_top_ - scroll_);

  // The fixed header over the scrolled map.
  canvas.FillRect({{0.0f, 0.0f}, {canvas.Size().x, view_top_ - 4.0f}}, ui_color::kBackground);
  canvas.FillRect({{0.0f, view_top_ - 6.0f}, {canvas.Size().x, view_top_ - 4.0f}}, WithAlpha(ui_color::kLine, 0.8f));
  const float top = safe.min.y;
  canvas.Text(font, "Threadlock", {safe.min.x + 16.0f, top + 28.0f}, 30.0f, Align::Left, Outlined(ui_color::kInk));
  const float right = safe.max.x;
  if (wlw::ui::IconButton(ui, Icon::Settings, {{right - 52.0f, top + 8.0f}, {right - 12.0f, top + 48.0f}})) {
    app_.GetFeedback().Play(synth::Sound::Tap);
    app_.OpenSettings();
  }
  if (wlw::ui::IconButton(ui, Icon::Trophy, {{right - 100.0f, top + 8.0f}, {right - 60.0f, top + 48.0f}})) {
    app_.GetFeedback().Play(synth::Sound::Tap);
    app_.OpenAchievements();
  }
  StarShape(canvas, {right - 158.0f, top + 21.0f}, 8.0f, ui_color::kStar);
  canvas.Text(font, std::to_string(progress.TotalStars()), {right - 146.0f, top + 21.0f}, 16.0f, Align::Left,
              Outlined(ui_color::kStar));
  wlw::ui::DrawIcon(canvas, Icon::Hint, {{right - 166.0f, top + 32.0f}, {right - 150.0f, top + 48.0f}}, ui_color::kInk);
  canvas.Text(font, std::to_string(progress.Hints()), {right - 146.0f, top + 40.0f}, 16.0f, Align::Left,
              Outlined(ui_color::kInk));

  // Daily and Lab cards.
  const float card_top = top + 60.0f, card_bottom = top + 176.0f;
  const float mid = (safe.min.x + safe.max.x) * 0.5f;
  const int today = app_.Today();
  const bool daily_done = progress.Daily().Marked(today);
  const int streak = progress.Daily().CurrentStreak(today);
  const std::string daily_sub = daily_done   ? "Solved today"
                                : streak > 0 ? "Streak " + std::to_string(streak)
                                             : "New puzzle";
  const wlw::ui::CardResult daily =
      wlw::ui::Card(ui, "Daily", daily_sub, {{safe.min.x + 12.0f, card_top}, {mid - 6.0f, card_bottom}},
                    LiquidColor(app_.GetPalette(), Color::Amber), daily_done ? "" : "NEW");
  wlw::ui::DrawIcon(canvas, daily_done ? Icon::Check : Icon::Clock,
                    {daily.art.min + glm::vec2(4.0f), daily.art.max - glm::vec2(4.0f)}, ui_color::kInk);
  if (daily.clicked) {
    app_.GetFeedback().Play(synth::Sound::Tap);
    app_.OpenDailyMenu();
  }
  const bool lab_open = progress.LabOpen(catalog);
  const wlw::ui::CardResult lab =
      wlw::ui::Card(ui, "The Lab", lab_open ? "Endless" : "After World 2",
                    {{mid + 6.0f, card_top}, {safe.max.x - 12.0f, card_bottom}},
                    lab_open ? LiquidColor(app_.GetPalette(), Color::Green) : ui_color::kFaint);
  wlw::ui::DrawIcon(canvas, lab_open ? Icon::Grid : Icon::Lock,
                    {lab.art.min + glm::vec2(4.0f), lab.art.max - glm::vec2(4.0f)}, ui_color::kInk);
  if (lab.clicked) {
    app_.GetFeedback().Play(synth::Sound::Tap);
    if (lab_open)
      app_.OpenLabMenu();
    else
      app_.Toast("Finish World 2 to open the Lab");
  }
}

void MapScreen::DrawWorld(UIContext& ui, wlw::ui::Canvas& picture, float scroll_top) {
  wlw::ui::Canvas& canvas = ui.canvas;
  const wlw::ui::Font& font = app_.GetFont();
  const Progress& progress = app_.GetProgress();
  const Catalog& catalog = app_.GetCatalog();
  const Palette& palette = app_.GetPalette();
  const float left = ui.safe_area.min.x;
  const float time = app_.Time();
  auto screen = [&](glm::vec2 at) { return glm::vec2(left + at.x, scroll_top + at.y); };
  auto visible = [&](float y) { return y > view_top_ - 60.0f && y < view_bottom_ + 60.0f; };
  const Level* current = progress.Current(catalog);

  // Pipes between consecutive main-path levels (bloomed: solved ones glow in the world's colour).
  const Spot* previous = nullptr;
  for (const Spot& spot : spots_) {
    if (spot.challenge) continue;
    if (previous && previous->level->world == spot.level->world) {  // worlds join at their headers
      const glm::vec2 a = screen(previous->at), b = screen(spot.at);
      if (visible(a.y) || visible(b.y)) {
        picture.Line(a, b, 14.0f, ui_color::kPipeEdge);
        picture.Line(a, b, 9.0f, ui_color::kPipeInside);
        if (progress.Solved(previous->level->id)) {
          const World* world = catalog.GetWorld(previous->level->world);
          const Rgba glow = LiquidColor(palette, world ? world->accent : Color::Blue);
          picture.Line(a, b, 5.0f, WithAlpha(glow, 0.9f));
          // A bead of light running along it.
          const float t = std::fmod(time * 0.6f + previous->at.y * 0.01f, 1.0f);
          picture.Circle(a + (b - a) * t, 4.0f, WithAlpha(glow, 1.0f));
        }
      }
    }
    previous = &spot;
  }

  for (const Header& header : headers_) {
    const float y = scroll_top + header.y;
    if (!visible(y)) continue;
    const World* world = catalog.GetWorld(header.world);
    if (!world) continue;
    const bool open = progress.WorldOpen(catalog, header.world);
    const Rgba accent = LiquidColor(palette, world->accent);
    canvas.Text(font, "World " + std::to_string(header.world), {left + 16.0f, y + 14.0f}, 14.0f, Align::Left,
                Outlined(open ? accent : ui_color::kFaint));
    canvas.Text(font, world->name, {left + 16.0f, y + 38.0f}, 26.0f, Align::Left,
                Outlined(open ? ui_color::kInk : ui_color::kFaint));
    const float right = ui.safe_area.max.x - 16.0f;
    const int stars = progress.WorldStars(catalog, header.world);
    const int most = static_cast<int>(catalog.MainPath(header.world).size()) * 3;
    if (open) {
      StarShape(canvas, {right - 64.0f, y + 26.0f}, 8.0f, ui_color::kStar);
      canvas.Text(font, std::to_string(stars) + " / " + std::to_string(most), {right, y + 26.0f}, 16.0f, Align::Right,
                  Outlined(ui_color::kStar));
    } else {
      const int need = Catalog::Gate(header.world);
      const int have = progress.StarsBefore(catalog, header.world);
      wlw::ui::DrawIcon(canvas, Icon::Lock, {{right - 120.0f, y + 16.0f}, {right - 100.0f, y + 36.0f}},
                        ui_color::kMuted);
      canvas.Text(font, std::to_string(have) + " / " + std::to_string(need) + " stars", {right, y + 26.0f}, 15.0f,
                  Align::Right, Outlined(ui_color::kMuted));
    }
  }

  for (const Spot& spot : spots_) {
    const glm::vec2 c = screen(spot.at);
    if (!visible(c.y)) continue;
    const Level& level = *spot.level;
    const World* world = catalog.GetWorld(level.world);
    const Rgba accent = LiquidColor(palette, world ? world->accent : Color::Blue);
    const bool open = progress.LevelOpen(catalog, level);
    const bool solved = progress.Solved(level.id);
    const int bits = progress.Bits(level.id);
    if (spot.challenge) {
      const float r = kNode * 0.95f;
      canvas.Triangle(c + glm::vec2(0.0f, -r), c + glm::vec2(r, 0.0f), c + glm::vec2(0.0f, r), ui_color::kPanelRaised);
      canvas.Triangle(c + glm::vec2(0.0f, -r), c + glm::vec2(0.0f, r), c + glm::vec2(-r, 0.0f), ui_color::kPanelRaised);
    } else {
      canvas.Circle(c, kNode, open ? ui_color::kPanelRaised : ui_color::kPanel);
      canvas.Ring(c, kNode, 2.5f, open ? (solved ? accent : ui_color::kInk) : WithAlpha(ui_color::kFaint, 0.6f));
    }
    if ((bits & kGoldFrame) && !spot.challenge) canvas.Ring(c, kNode + 5.0f, 2.0f, ui_color::kStar);
    if (&level == current && !solved) {
      canvas.Ring(c, kNode + 6.0f + 3.0f * std::sin(time * 4.0f), 2.5f, WithAlpha(ui_color::kGood, 0.8f));
    }
    if (open && spot.challenge) {
      StarShape(canvas, c, 11.0f, ui_color::kStar);
    } else if (open) {
      canvas.Text(font, std::to_string(level.number), c, 20.0f, Align::Center, Outlined(ui_color::kInk));
    } else {
      wlw::ui::DrawIcon(canvas, Icon::Lock, {c - glm::vec2(9.0f), c + glm::vec2(9.0f)},
                        WithAlpha(ui_color::kFaint, 0.8f));
    }
    // Its stars under it.
    const int masks[3] = {kSolveStar, kLeanStar, kSwiftStar};
    for (int i = 0; i < 3; ++i) {
      const glm::vec2 s = c + glm::vec2((static_cast<float>(i) - 1.0f) * 13.0f, kNode + 11.0f);
      StarShape(canvas, s, 5.5f, (bits & masks[i]) ? ui_color::kStar : WithAlpha(ui_color::kFaint, 0.35f));
    }
    if (spot.challenge && open) {
      canvas.Text(font, level.name, c + glm::vec2(0.0f, -kNode - 10.0f), 12.0f, Align::Center,
                  Outlined(ui_color::kMuted));
    }
  }
  // Challenge rows get a label.
  for (const Header& header : headers_) {
    for (const Spot& spot : spots_) {
      if (!spot.challenge || spot.level->world != header.world) continue;
      const glm::vec2 c = screen(spot.at);
      if (visible(c.y)) {
        canvas.Text(font, "Challenges", {left + 16.0f, c.y - 50.0f}, 13.0f, Align::Left, Outlined(ui_color::kStar));
      }
      break;
    }
  }
}

}  // namespace threadlock
