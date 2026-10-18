#include "menus.h"

#include <algorithm>
#include <string>
#include <string_view>

#include "generator.h"
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

constexpr const char* kVersion = "Threadlock 0.2";
constexpr const char* kWeekdays[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};

}  // namespace

void MenuScreen::Close() {
  app_.GetFeedback().Play(synth::Sound::Tap);
  app_.GetProgress().Save();
  Stack().Pop();
}

bool MenuScreen::OnBack() {
  Close();
  return true;
}

void MenuScreen::RenderUI(UIContext& ui) {
  switch (kind_) {
    case Kind::Settings: Settings(ui); break;
    case Kind::Achievements: Achievements(ui); break;
    case Kind::Daily: Daily(ui); break;
    case Kind::Lab: Lab(ui); break;
  }
}

void MenuScreen::Settings(UIContext& ui) {
  wlw::ui::Canvas& canvas = ui.canvas;
  const wlw::ui::Font& font = app_.GetFont();
  Progress& progress = app_.GetProgress();
  const bool privacy = app_.GetAds().PrivacyOptionsRequired();
  const wlw::ui::DialogResult dialog = wlw::ui::Dialog(ui, "Settings", {330.0f, privacy ? 692.0f : 640.0f}, true);
  if (dialog.close) {
    Close();
    return;
  }
  const Rect& r = dialog.content;
  float y = r.min.y + 4.0f;
  auto row = [&](float height) {
    const Rect rect{{r.min.x + 6.0f, y}, {r.max.x - 6.0f, y + height}};
    y += height + 8.0f;
    return rect;
  };
  threadlock::Settings& settings = progress.settings;
  // Four switches, six palettes and the links in a dialog that fits a phone: rows a little tighter.
  if (wlw::ui::SettingRow(ui, Icon::Sound, "Sound", settings.sound, row(42.0f))) {
    progress.SettingsChanged();
    app_.ApplySettings();
  }
  if (wlw::ui::SettingRow(ui, Icon::Music, "Music", settings.music, row(42.0f))) {
    progress.SettingsChanged();
    app_.ApplySettings();
  }
  if (wlw::ui::SettingRow(ui, Icon::Vibration, "Vibration", settings.haptics, row(42.0f))) {
    progress.SettingsChanged();
    app_.ApplySettings();
    app_.GetFeedback().Vibrate(30, 0.7f);
  }
  if (wlw::ui::SettingRow(ui, Icon::Clock, "Reduced motion", settings.reduced_motion, row(42.0f)))
    progress.SettingsChanged();

  canvas.Text(font, "Liquid colours", {r.min.x + 10.0f, y + 10.0f}, 16.0f, Align::Left, Outlined(ui_color::kMuted));
  y += 24.0f;
  for (const Palette& palette : kPalettes) {
    const Rect rect = row(32.0f);
    const bool owned = progress.PaletteOwned(app_.GetCatalog(), palette.key);
    const bool chosen = settings.palette == palette.key;
    const wlw::ui::Press press = wlw::ui::Pressable(ui, wlw::ui::WidgetId(palette.key, rect), rect);
    canvas.FillRect(rect, chosen ? ui.theme.surface_raised : WithAlpha(ui.theme.track, 0.7f), ui.theme.radius_small);
    if (chosen) canvas.Ring({rect.max.x - 18.0f, rect.Center().y}, 8.0f, 2.5f, ui_color::kGood);
    const Rgba swatches[] = {palette.blue, palette.amber, palette.green, palette.rose, palette.pearl};
    for (int k = 0; k < 5; ++k) {
      canvas.Circle({rect.min.x + 18.0f + static_cast<float>(k) * 17.0f, rect.Center().y}, 7.0f,
                    WithAlpha(swatches[k], owned ? 1.0f : 0.3f));
    }
    canvas.Text(font, palette.name, {rect.min.x + 108.0f, rect.Center().y - (owned ? 0.0f : 7.0f)}, 16.0f, Align::Left,
                Outlined(owned ? ui_color::kInk : ui_color::kFaint));
    if (!owned) {
      canvas.Text(font, Progress::PaletteUnlockText(palette.key), {rect.min.x + 108.0f, rect.Center().y + 9.0f}, 11.0f,
                  Align::Left, Outlined(ui_color::kMuted));
      wlw::ui::DrawIcon(canvas, Icon::Lock,
                        {{rect.max.x - 26.0f, rect.Center().y - 8.0f}, {rect.max.x - 10.0f, rect.Center().y + 8.0f}},
                        ui_color::kFaint);
    }
    if (press.clicked && owned && !chosen) {
      settings.palette = palette.key;
      progress.SettingsChanged();
      app_.GetFeedback().Play(synth::Sound::Tap);
    }
  }
  y += 4.0f;
  if (privacy && wlw::ui::LinkRow(ui, Icon::Privacy, "Privacy options", row(44.0f))) {
    app_.GetAds().ShowPrivacyOptions();
  }
  if (wlw::ui::LinkRow(ui, Icon::Trophy, "Achievements", row(44.0f))) {
    Stack().Replace(std::make_unique<MenuScreen>(app_, Kind::Achievements));
  }
  canvas.Text(font, kVersion, {r.Center().x, y + 6.0f}, 12.0f, Align::Center, Outlined(ui_color::kFaint));
}

void MenuScreen::Achievements(UIContext& ui) {
  wlw::ui::Canvas& canvas = ui.canvas;
  const wlw::ui::Font& font = app_.GetFont();
  const Progress& progress = app_.GetProgress();
  const float height = 70.0f + static_cast<float>(std::size(kAchievements)) * 64.0f;
  const wlw::ui::DialogResult dialog = wlw::ui::Dialog(ui, "Achievements", {330.0f, height}, true);
  if (dialog.close) {
    Close();
    return;
  }
  const Rect& r = dialog.content;
  float y = r.min.y + 4.0f;
  for (const Achievement& achievement : kAchievements) {
    const Rect rect{{r.min.x + 6.0f, y}, {r.max.x - 6.0f, y + 56.0f}};
    y += 64.0f;
    const bool done = progress.Achieved(achievement.key);
    const int have = progress.AchievementProgress(achievement);
    canvas.FillRect(rect, done ? ui.theme.surface_raised : WithAlpha(ui.theme.track, 0.7f), ui.theme.radius_small);
    wlw::ui::DrawIcon(canvas, done ? Icon::Trophy : Icon::Lock,
                      {{rect.min.x + 10.0f, rect.Center().y - 13.0f}, {rect.min.x + 36.0f, rect.Center().y + 13.0f}},
                      done ? ui_color::kStar : ui_color::kFaint);
    canvas.Text(font, achievement.name, {rect.min.x + 48.0f, rect.min.y + 17.0f}, 17.0f, Align::Left,
                Outlined(done ? ui_color::kInk : ui_color::kMuted));
    canvas.Text(font, achievement.text, {rect.min.x + 48.0f, rect.min.y + 38.0f}, 12.0f, Align::Left,
                Outlined(ui_color::kMuted));
    if (achievement.goal > 1 && !done) {
      const float w = 60.0f;
      const glm::vec2 bar{rect.max.x - w - 10.0f, rect.min.y + 14.0f};
      canvas.FillRect({bar, bar + glm::vec2(w, 6.0f)}, ui.theme.track, 3.0f);
      canvas.FillRect({bar, bar + glm::vec2(w * static_cast<float>(have) / static_cast<float>(achievement.goal), 6.0f)},
                      ui_color::kGood, 3.0f);
      canvas.Text(font, std::to_string(have) + "/" + std::to_string(achievement.goal),
                  {rect.max.x - 10.0f, rect.min.y + 32.0f}, 12.0f, Align::Right, Outlined(ui_color::kMuted));
    }
  }
}

void MenuScreen::Daily(UIContext& ui) {
  wlw::ui::Canvas& canvas = ui.canvas;
  const wlw::ui::Font& font = app_.GetFont();
  Progress& progress = app_.GetProgress();
  const wlw::ui::DialogResult dialog = wlw::ui::Dialog(ui, "Daily puzzle", {330.0f, 460.0f}, true);
  if (dialog.close) {
    Close();
    return;
  }
  const Rect& r = dialog.content;
  const int today = app_.Today();
  const wlw::kit::Date date = wlw::kit::Date::FromNumber(today);
  const int weekday = DailyWeekday(today);
  canvas.Text(font,
              std::string(kWeekdays[weekday]) + " " + std::to_string(date.day) + "." + std::to_string(date.month) +
                  " - " + DailyDifficultyName(DailyDifficulty(today)),
              {r.Center().x, r.min.y + 14.0f}, 17.0f, Align::Center, Outlined(ui_color::kInk));
  canvas.Text(font, "Easy on Monday, hard on Sunday. The same for everyone.", {r.Center().x, r.min.y + 38.0f}, 12.0f,
              Align::Center, Outlined(ui_color::kMuted));
  const int streak = progress.Daily().CurrentStreak(today);
  const int best = progress.Daily().BestStreak();
  canvas.Text(font, "Streak " + std::to_string(streak) + ",  best " + std::to_string(best),
              {r.Center().x, r.min.y + 68.0f}, 18.0f, Align::Center, Outlined(ui_color::kStar));
  const bool done = progress.Daily().Marked(today);
  if (wlw::ui::Button(ui, done ? "Play again" : "Play today's puzzle",
                      {{r.min.x + 10.0f, r.min.y + 92.0f}, {r.max.x - 10.0f, r.min.y + 142.0f}},
                      done ? Variant::Secondary : Variant::Primary, Icon::Play)) {
    app_.GetFeedback().Play(synth::Sound::Tap);
    Stack().Pop();
    app_.OpenDaily(today, false);
    return;
  }
  canvas.Text(font, "The last week", {r.min.x + 10.0f, r.min.y + 166.0f}, 15.0f, Align::Left,
              Outlined(ui_color::kMuted));
  const float cell = (r.Size().x - 20.0f) / 7.0f;
  wlw::kit::Ads& ads = app_.GetAds();
  for (int k = 0; k < 7; ++k) {
    const int day = date.AddDays(k - 7).ToNumber();
    const bool solved = progress.Daily().Marked(day);
    const Rect rect{{r.min.x + 10.0f + cell * static_cast<float>(k) + 2.0f, r.min.y + 182.0f},
                    {r.min.x + 10.0f + cell * static_cast<float>(k + 1) - 2.0f, r.min.y + 252.0f}};
    const wlw::ui::Press press = wlw::ui::Pressable(ui, wlw::ui::WidgetId("day" + std::to_string(day), rect), rect);
    canvas.FillRect(rect, solved ? ui.theme.surface_raised : WithAlpha(ui.theme.track, 0.8f), 8.0f);
    canvas.Text(font, kWeekdays[DailyWeekday(day)], {rect.Center().x, rect.min.y + 16.0f}, 13.0f, Align::Center,
                Outlined(ui_color::kMuted));
    wlw::ui::DrawIcon(canvas, solved ? Icon::Check : Icon::Play,
                      {rect.Center() - glm::vec2(10.0f, -6.0f), rect.Center() + glm::vec2(10.0f, 26.0f)},
                      solved ? ui_color::kGood : ui_color::kInk);
    if (press.clicked && !solved) {
      if (!ads.RewardedReady()) {
        app_.Toast("No ad right now: try again soon");
      } else {
        ThreadlockApp& app = app_;
        app.OfferRewarded([&app, day] { app.OpenDaily(day, true); });
        Stack().Pop();
        return;
      }
    }
  }
  const bool mend = progress.CanMendStreak(today);
  canvas.Text(font, "Missed a day? Watch an ad to play it.", {r.Center().x, r.min.y + 276.0f}, 13.0f, Align::Center,
              Outlined(ui_color::kMuted));
  canvas.Text(font, mend ? "Once a week, that also mends your streak." : "Your streak mend is used for this week.",
              {r.Center().x, r.min.y + 296.0f}, 13.0f, Align::Center, Outlined(ui_color::kMuted));
  canvas.Text(font, "Each daily solved: +1 hint", {r.Center().x, r.max.y - 14.0f}, 13.0f, Align::Center,
              Outlined(ui_color::kGood));
}

void MenuScreen::Lab(UIContext& ui) {
  wlw::ui::Canvas& canvas = ui.canvas;
  const wlw::ui::Font& font = app_.GetFont();
  Progress& progress = app_.GetProgress();
  const wlw::ui::DialogResult dialog = wlw::ui::Dialog(ui, "The Lab", {330.0f, 330.0f}, true);
  if (dialog.close) {
    Close();
    return;
  }
  const Rect& r = dialog.content;
  canvas.Text(font, "Endless machines, made for you.", {r.Center().x, r.min.y + 16.0f}, 16.0f, Align::Center,
              Outlined(ui_color::kInk));
  static constexpr std::string_view kLabels[] = {"Easy", "Medium", "Hard"};
  static constexpr const char* kKeys[] = {"easy", "medium", "hard"};
  int selected = 1;
  for (int i = 0; i < 3; ++i) {
    if (progress.lab_difficulty == kKeys[i]) selected = i;
  }
  if (wlw::ui::Segmented(ui, kLabels, selected,
                         {{r.min.x + 10.0f, r.min.y + 44.0f}, {r.max.x - 10.0f, r.min.y + 88.0f}})) {
    progress.lab_difficulty = kKeys[selected];
    progress.LabChanged();
    app_.GetFeedback().Play(synth::Sound::Tap);
  }
  canvas.Text(font, "Solved in the Lab: " + std::to_string(progress.Counter("lab_solved")),
              {r.Center().x, r.min.y + 116.0f}, 15.0f, Align::Center, Outlined(ui_color::kMuted));
  canvas.Text(font, "Hints here come from ads.", {r.Center().x, r.min.y + 138.0f}, 13.0f, Align::Center,
              Outlined(ui_color::kMuted));
  if (wlw::ui::Button(ui, "Brew a puzzle", {{r.min.x + 10.0f, r.max.y - 70.0f}, {r.max.x - 10.0f, r.max.y - 20.0f}},
                      Variant::Primary, Icon::Play)) {
    app_.GetFeedback().Play(synth::Sound::Tap);
    Stack().Pop();
    app_.OpenLab();
  }
}

}  // namespace threadlock
