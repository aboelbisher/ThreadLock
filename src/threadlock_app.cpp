#include "threadlock_app.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "generator.h"
#include "level_screen.h"
#include "map_screen.h"
#include "menus.h"
#include "ui_parts.h"
#include "wlw/core/cvar.h"
#include "wlw/core/log.h"
#include "wlw/kit/daily.h"
#include "wlw/platform/platform.h"
#include "wlw/rendering/shader.h"
#include "wlw/ui/widgets.h"

#if WLW_DEBUG_UI
#include "imgui.h"
#endif

namespace threadlock {
namespace {

// Development shortcuts (adb: am start ... --es wlw "tl.open=1-6;tl.solve=1;tl.autoplay=2").
wlw::CVar<std::string> cv_open{"tl.open", "", "Open this level at startup (dev)"};
wlw::CVar<bool> cv_calm{"tl.calm", false, "Reduced motion, whatever the setting (dev)"};

// Ads: Google's test units in every build until Threadlock has its own AdMob app (DESIGN.md §10;
// the app id in AndroidManifest.xml is Google's sample one too). Real units go here, release only,
// exactly as Bluzzle does.
wlw::kit::AdsConfig MakeAdsConfig() {
  wlw::kit::AdsConfig config;
  config.test_devices = {};
  config.debug_consent_eea = false;
  return config;
}

// The pacing from DESIGN.md §10: the first 10 solved levels are ad-free, then at most one
// interstitial every 3 solved levels, never twice within 3 minutes.
wlw::kit::AdPacing::Rules PacingRules() {
  return {.first_after_games = 10, .games_between = 3, .min_seconds_between = 180.0};
}

}  // namespace

wlw::app::AppConfig ThreadlockApp::MakeConfig() {
  wlw::app::AppConfig config;
  config.title = "Threadlock";
  config.window_size = {450, 950};
  config.ui_reference_size = {360.0f, 800.0f};
  return config;
}

void ThreadlockApp::OnStart() {
  font_ = wlw::ui::Font::Load("fonts/LilitaOne-Regular.ttf");
  hand_ = wlw::ui::Font::Load("fonts/Caveat-Bold.ttf");
  street_ = wlw::ui::Font::Load("fonts/CormorantSC-Bold.ttf");
  sigil_ = wlw::ui::Font::Load("fonts/Cinzel-Bold.ttf");
  progress_ = std::make_unique<Progress>(wlw::platform::GetSaveDirectory("Threadlock"));
  progress_->Load();
  if (!catalog_.Load()) WLW_LOG_ERROR("Some levels didn't load");
  feedback_.Load(App().Audio());
  ApplySettings();
  liquid_.Create(App());
  lantern_art_.Create(App());
  apothecary_art_.Create(App(), hand_);
  canal_art_.Create(App(), street_);
  sanctum_art_.Create(App(), sigil_);
  city_art_.Create(App());
  action_material_ =
      wlw::rendering::ShaderMaterial::Create(wlw::rendering::Shader::FromFile("shaders/ui_gradient.shader"));
  // Toasts and the desktop's stand-in ads draw over every screen, on a layer above the game UI.
  top_canvas_ = &App().Window().GetScreen().AddCanvasLayer({.mode = wlw::scene::CanvasMode::Overlay, .order = 10});
  App().Quality().Configure({.levels = 3});
  App().Quality().OnChange([this](int level) { liquid_.ApplyQuality(App(), level); });  // also once now
  ads_ = wlw::kit::Ads::Create(MakeAdsConfig());
  pacing_ = wlw::kit::AdPacing(PacingRules());
  // Solves from earlier sessions count, so "the first 10 levels are ad-free" means the first 10 ever.
  const int games = progress_->AdGames();
  for (int i = 0; i < games; ++i) pacing_.GameFinished();
  if (games >= pacing_.rules.first_after_games) pacing_.InterstitialShown(-1000.0);

  // First launch: no menu, level 1-1 straight away. The map appears after 1-3.
  const Level* current = progress_->Current(catalog_);
  if (const Level* wanted = catalog_.Find(cv_open.Get())) {
    OpenLevel(wanted);
    return;
  }
  if (!progress_->MapOpen() && current) {
    OpenLevel(current);
  } else {
    OpenMap(current);
  }
}

void ThreadlockApp::ApplySettings() {
  const Settings& settings = progress_->settings;
  feedback_.SetSettings(settings.sound, settings.haptics, settings.music);
}

BoardArt& ThreadlockApp::ArtFor(const Level& level) {
  switch (LookForWorld(level.world).art) {
    case ArtKind::Lantern: return lantern_art_;
    case ArtKind::Apothecary: return apothecary_art_;
    case ArtKind::Canal:
      if (canal::IsCanalMap(level)) return canal_art_;
      break;
    case ArtKind::Sanctum:
      if (sigil::IsSigil(level)) return sanctum_art_;
      break;
    case ArtKind::Workshop: break;
  }
  return workshop_art_;
}

int ThreadlockApp::Today() const { return wlw::kit::Today().ToNumber(); }

void ThreadlockApp::OnUpdate(float dt) {
  time_ += dt;
  ads_->Update(dt);
  feedback_.Update();
  for (ToastItem& toast : toasts_) toast.age += dt;
  while (!toasts_.empty() && toasts_.front().age > 2.6f) toasts_.pop_front();
}

void ThreadlockApp::OnRenderUI(wlw::ui::UIContext& ui) {
  SetupTheme(ui.theme, font_.get());
  liquid_.SetCalm(Calm());
  DrawToasts(ui);
  DrawFakeAd(ui);
}

bool ThreadlockApp::OnBack() { return false; }

bool ThreadlockApp::Calm() const { return progress_->settings.reduced_motion || cv_calm.Get(); }

void ThreadlockApp::OnAppPaused() { progress_->Save(); }

void ThreadlockApp::OnShutdown() {
  progress_->Save();
  feedback_.Shutdown();
}

// ---- Moving between screens ----

void ThreadlockApp::OpenLevel(const Level* level, const LevelSource& source) {
  if (!level) return;
  App().Screens().Reset(std::make_unique<LevelScreen>(*this, level, source), wlw::app::ScreenStack::Transition::Fade);
}

void ThreadlockApp::OpenMap(const Level* focus) {
  App().Screens().Reset(std::make_unique<MapScreen>(*this, focus), wlw::app::ScreenStack::Transition::Fade);
}

const Level* ThreadlockApp::Keep(Level level) {
  kept_.push_back(std::move(level));
  while (kept_.size() > 6) kept_.pop_front();  // never the one just added: screens use the newest
  return &kept_.back();
}

void ThreadlockApp::OpenDaily(int date, bool archive) {
  const Level* level = Keep(GenerateDaily(date));
  WLW_LOG_INFO("Daily %d: %s, pars %d pieces / %d ticks", date, level->name.c_str(), level->pars.pieces,
               level->pars.ticks);
  OpenLevel(level, {.kind = LevelSource::Daily, .date = date, .archive = archive, .difficulty = DailyDifficulty(date)});
}

void ThreadlockApp::OpenLab() {
  const int64_t seed = progress_->lab_seed++;
  progress_->LabChanged();
  const Level* level = Keep(GenerateLab(seed, progress_->lab_difficulty));
  WLW_LOG_INFO("Lab %lld (%s): %s", static_cast<long long>(seed), progress_->lab_difficulty.c_str(),
               level->name.c_str());
  OpenLevel(level, {.kind = LevelSource::Lab});
}

void ThreadlockApp::OpenSettings() {
  App().Screens().Push(std::make_unique<MenuScreen>(*this, MenuScreen::Kind::Settings));
}

void ThreadlockApp::OpenAchievements() {
  App().Screens().Push(std::make_unique<MenuScreen>(*this, MenuScreen::Kind::Achievements));
}

void ThreadlockApp::OpenDailyMenu() {
  App().Screens().Push(std::make_unique<MenuScreen>(*this, MenuScreen::Kind::Daily));
}

void ThreadlockApp::OpenLabMenu() { App().Screens().Push(std::make_unique<MenuScreen>(*this, MenuScreen::Kind::Lab)); }

// ---- Ads ----

void ThreadlockApp::OfferRewarded(std::function<void()> reward) {
  if (!ads_->RewardedReady()) {
    Toast("No ad right now: try again soon");
    return;
  }
  ads_->ShowRewarded([this, reward = std::move(reward)](bool rewarded) {
    pacing_.RewardedWatched(App().Time().SinceStart());  // pushes the next interstitial back
    if (rewarded) {
      reward();
    } else {
      Toast("No reward: the ad closed early");
    }
  });
}

void ThreadlockApp::LevelSolved() {
  pacing_.GameFinished();
  progress_->SetAdGames(std::min(progress_->AdGames() + 1, 100000));
}

void ThreadlockApp::BetweenLevels(bool allowed, std::function<void()> then) {
  const double now = App().Time().SinceStart();
  if (allowed && pacing_.InterstitialDue(now) && ads_->InterstitialReady()) {
    ads_->ShowInterstitial([this, then = std::move(then)] {
      pacing_.InterstitialShown(App().Time().SinceStart());
      then();
    });
    return;
  }
  then();
}

// ---- Toasts and the desktop's fake ad ----

void ThreadlockApp::Toast(std::string text) {
  for (const ToastItem& toast : toasts_) {
    if (toast.text == text) return;
  }
  toasts_.push_back({std::move(text), 0.0f});
  feedback_.Play(synth::Sound::Star, 0.4f);
}

void ThreadlockApp::DrawToasts(wlw::ui::UIContext& ui) {
  wlw::ui::Canvas& canvas = *top_canvas_;
  float y = ui.safe_area.min.y + 70.0f;
  for (const ToastItem& toast : toasts_) {
    const float in = std::min(1.0f, toast.age * 6.0f), out = std::clamp((2.6f - toast.age) * 3.0f, 0.0f, 1.0f);
    const float a = std::min(in, out);
    const float width = font_->Measure(toast.text, 17.0f) + 36.0f;
    const glm::vec2 c{ui.safe_area.Center().x, y};
    canvas.FillRect({c - glm::vec2(width * 0.5f, 18.0f), c + glm::vec2(width * 0.5f, 18.0f)}, Hex(0x16212D, 0.95f * a),
                    18.0f);
    canvas.Text(*font_, toast.text, c, 17.0f, wlw::ui::Align::Center, Outlined(ui_color::kStar, a));
    y += 44.0f;
  }
}

void ThreadlockApp::DrawFakeAd(wlw::ui::UIContext& ui) {
  auto* fake = dynamic_cast<wlw::kit::FakeAds*>(ads_.get());
  if (!fake || !fake->Showing()) return;
  wlw::ui::Canvas& canvas = *top_canvas_;
  canvas.FillRect({{0.0f, 0.0f}, canvas.Size()}, Hex(0x0B0B0F));
  const glm::vec2 c = ui.safe_area.Center();
  canvas.Text(*font_, fake->ShowingRewarded() ? "REWARDED AD" : "AD", c - glm::vec2(0.0f, 60.0f), 44.0f,
              wlw::ui::Align::Center, Outlined(ui_color::kInk));
  canvas.Text(*font_, "(desktop stand-in for a real ad)", c - glm::vec2(0.0f, 16.0f), 16.0f, wlw::ui::Align::Center,
              Outlined(ui_color::kMuted));
  canvas.FillRect({c + glm::vec2(-110.0f, 16.0f), c + glm::vec2(110.0f, 26.0f)}, Hex(0x262A33), 5.0f);
  canvas.FillRect(
      {c + glm::vec2(-110.0f, 16.0f), c + glm::vec2(-110.0f + 220.0f * std::min(fake->Progress(), 1.0f), 26.0f)},
      ui_color::kStar, 5.0f);
}

// ---- Debug panel (debug builds) ----

void ThreadlockApp::OnDebugUI() {
#if WLW_DEBUG_UI
  if (!ImGui::Begin("Threadlock")) {
    ImGui::End();
    return;
  }
  ImGui::Text("Stars %d, hints %d, solves %lld", progress_->TotalStars(), progress_->Hints(),
              static_cast<long long>(progress_->Counter("solves")));
  if (ImGui::Button("+5 hints")) progress_->AddHints(5);
  ImGui::SameLine();
  if (ImGui::Button("Open everything")) {
    for (const World& world : catalog_.Worlds()) {
      for (const Level& level : world.levels) progress_->RecordSolve(level, level.pars.pieces, level.pars.ticks, false);
    }
    progress_->Save();
  }
  ImGui::SameLine();
  if (ImGui::Button("Wipe progress")) {
    progress_->Wipe();
    progress_->Save();
    OpenLevel(progress_->Current(catalog_));
  }
  if (ImGui::CollapsingHeader("Levels", ImGuiTreeNodeFlags_DefaultOpen)) {
    for (const World& world : catalog_.Worlds()) {
      ImGui::Text("World %d", world.number);
      for (const Level& level : world.levels) {
        ImGui::SameLine();
        if (ImGui::SmallButton(level.id.c_str())) OpenLevel(&level);
      }
    }
    if (ImGui::Button("Today's daily")) OpenDaily(Today(), false);
    ImGui::SameLine();
    if (ImGui::Button("Lab")) OpenLab();
    ImGui::SameLine();
    if (ImGui::Button("Map")) OpenMap();
  }
  if (ImGui::CollapsingHeader("Ads")) {
    ImGui::TextWrapped("%s", ads_->Describe().c_str());
    const double now = App().Time().SinceStart();
    ImGui::Text("Pacing: %d solved, %d since the last ad; interstitial %s", pacing_.GamesFinished(),
                pacing_.GamesSinceAd(), pacing_.InterstitialDue(now) ? "DUE" : "not due");
    if (ImGui::Button("Show rewarded")) OfferRewarded([this] { Toast("Rewarded!"); });
    ImGui::SameLine();
    if (ImGui::Button("Show interstitial")) ads_->ShowInterstitial([] {});
  }
  ImGui::End();
#endif
}

}  // namespace threadlock
