#pragma once

#include <deque>
#include <functional>
#include <memory>
#include <string>

#include "board_art.h"
#include "catalog.h"
#include "feedback.h"
#include "lantern_art.h"
#include "liquid.h"
#include "palette.h"
#include "progress.h"
#include "workshop_art.h"
#include "world_look.h"
#include "wlw/app/application.h"
#include "wlw/app/system.h"
#include "wlw/kit/ad_pacing.h"
#include "wlw/kit/ads.h"
#include "wlw/rendering/shader_material.h"
#include "wlw/ui/font.h"
#include "wlw/ui/ui_context.h"

// The game's one system: the services every screen shares (levels, progress, sounds, the liquid,
// ads) and the moves between screens. The screens (level_screen.h, map_screen.h, menus.h) are
// the game's states on the engine's ScreenStack.
namespace threadlock {

// Where a level came from: the handmade worlds, today's (or an archived) daily puzzle, the Lab.
struct LevelSource {
  enum Kind { Main, Daily, Lab } kind = Main;
  int date = 0;          // Daily: yyyymmdd
  bool archive = false;  // Daily: a missed day, played from the archive
  int difficulty = 0;    // Daily / Lab
};

class ThreadlockApp : public wlw::app::System {
 public:
  static wlw::app::AppConfig MakeConfig();

  void OnStart() override;
  void OnUpdate(float dt) override;
  void OnRenderUI(wlw::ui::UIContext& ui) override;
  void OnDebugUI() override;
  bool OnBack() override;
  void OnAppPaused() override;
  void OnShutdown() override;

  // ---- Services ----
  Catalog& GetCatalog() { return catalog_; }
  Progress& GetProgress() { return *progress_; }
  Feedback& GetFeedback() { return feedback_; }
  Liquid& GetLiquid() { return liquid_; }
  // The big EDIT / PLAY button's gradient (assets/shaders/ui_gradient.shader).
  const std::shared_ptr<wlw::rendering::ShaderMaterial>& ActionMaterial() const { return action_material_; }
  // How a world's boards are drawn (world 0 = the daily and the Lab).
  BoardArt& ArtFor(int world);
  const wlw::ui::Font& GetFont() const { return *font_; }
  const Palette& GetPalette() const { return PaletteByKey(progress_->settings.palette); }
  wlw::app::Application& Services() { return App(); }
  float Time() const { return time_; }
  bool Calm() const { return progress_->settings.reduced_motion; }  // the reduced-motion setting
  int Today() const;
  void ApplySettings();  // progress settings -> sound, haptics

  // ---- Moving between screens ----
  void OpenLevel(const Level* level, const LevelSource& source = {});
  void OpenMap(const Level* focus = nullptr);
  void OpenDaily(int date, bool archive);
  void OpenLab();
  void OpenSettings();
  void OpenAchievements();
  void OpenDailyMenu();
  void OpenLabMenu();
  // A generated level (daily / Lab), kept alive as long as a screen may use it.
  const Level* Keep(Level level);

  // ---- Ads (DESIGN.md §10) ----
  wlw::kit::Ads& GetAds() { return *ads_; }
  // A rewarded ad the player chose to watch: `reward` runs if they watched it through.
  void OfferRewarded(std::function<void()> reward);
  // A solved level counts towards the interstitial pacing.
  void LevelSolved();
  // Before leaving a solved level (Next / Map): shows an interstitial if one is due and allowed,
  // then runs `then` (at once if none). `allowed` = our own rules (not after a finale, the daily...).
  void BetweenLevels(bool allowed, std::function<void()> then);

  // A short message over everything (a new achievement, a reward).
  void Toast(std::string text);

 private:
  void DrawFakeAd(wlw::ui::UIContext& ui);
  void DrawToasts(wlw::ui::UIContext& ui);
  void SaveAdPacing();

  Catalog catalog_;
  std::unique_ptr<Progress> progress_;
  Feedback feedback_;
  Liquid liquid_;
  WorkshopArt workshop_art_;
  LanternArt lantern_art_;
  std::shared_ptr<wlw::rendering::ShaderMaterial> action_material_;
  std::shared_ptr<wlw::ui::Font> font_;
  std::unique_ptr<wlw::kit::Ads> ads_;
  wlw::kit::AdPacing pacing_;
  std::deque<Level> kept_;  // generated levels (a deque: pointers stay valid as it grows)
  struct ToastItem {
    std::string text;
    float age = 0.0f;
  };
  std::deque<ToastItem> toasts_;
  float time_ = 0.0f;
  wlw::ui::Canvas* top_canvas_ = nullptr;  // an Overlay layer above the game UI: toasts, the desktop's fake ads
};

}  // namespace threadlock
