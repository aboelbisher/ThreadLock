#pragma once

#include <array>
#include <functional>
#include <limits>
#include <optional>
#include <vector>

#include <glm/glm.hpp>

#include "board_camera.h"
#include "city_art.h"
#include "city_map.h"
#include "threadlock_app.h"
#include "wlw/app/screen.h"

// The city map (DESIGN.md §5, §8): the level select. A tall picture of the city you scroll down: World
// 1's tower with the rose window (progress, not buttons) and a path of numbered lanterns (one per level,
// its stars under it), then the districts of worlds 2-4 behind their star gates. Daily and the Lab are
// round buttons in the bottom corners; stars, hints, trophies and settings sit in a slim top bar.
//
// What's new since the map was last seen (city::WhatsNew against the record in device.json) plays as
// an animation: the view glides to the lantern, it lights and its stars pop, the window's petal lights
// (a glide up to it if it's off screen), then the next lantern breathes; a gate's valve turns and light
// pours down to the district's first lamp. In the canal quarter a solve lights the harbour's neighbourhood,
// then the light runs on down the canal to the next harbour, the view riding it. A tap skips it.
namespace threadlock {

class MapScreen : public wlw::app::Screen {
 public:
  MapScreen(ThreadlockApp& app, const Level* focus);

  void OnEnter() override;
  void OnExit() override;
  void Update(float dt) override;
  void RenderUI(wlw::ui::UIContext& ui) override;
  bool OnBack() override { return false; }  // the app goes to the background

 private:
  static constexpr float kNever = std::numeric_limits<float>::infinity();  // not lit
  static constexpr float kLongAgo = -1.0e9f;                               // lit before the map opened

  void Build();
  void Start(float view);  // the first frame: the news, the camera
  void SnapTo(float scroll);
  void Glide(float to, float seconds);
  void At(float delay, std::function<void()> run);  // from now
  float LightLantern(int lantern, int fresh, float start);
  float LightPanes(int lantern, int fresh, float start);
  float LightLamp(int world, int lamp, int fresh, float start);
  float FillBottle(int lamp, int fresh, float start);  // World 2's shop window: fill, cork, label, gold, the bell
  float OpenGate(int world, float start);
  void Skip();  // a tap during an animation: everything as it ends
  void ShowAll();
  // A level's stars `bits` shown from `when` on (its lantern and panes, or its lamp and, in the market, its bottle).
  // window: World 2's bottle, World 3's neighbourhood too.
  void Show(int world, int index, int bits, float when, bool window = true);
  bool Busy() const { return clock_ < busy_until_; }
  float Chime();  // the next note up the scale (a pitch)
  // World 3's canal: the light runs from `from` to `to` (along it) starting at clock_ + `delay`, the view riding its
  // head; returns its seconds. CanalRest: the canal lit down to the next lamp shown (none while the gate is shut).
  float PourCanal(float from, float to, float delay);
  void CanalRest();
  float CanalShown() const;                   // how far the canal is lit now
  int NextLamp() const;                       // World 3's first harbour not lit yet, -1 if all are
  float ScrollFor(const Level* level) const;  // the view that shows a level's lantern or lamp

  bool LockedShown(int lantern) const;
  bool LampLockedShown(int world, int lamp) const;  // worlds 2 and 3, the same way (World 3: once the canal reaches it)
  bool MarketInView() const;                        // the open market square fills the middle of the view
  bool QuarterInView() const;                       // and the canal quarter
  const StarMap& Stars() const;                     // the saved stars, or tl.stars' (dev)
  const Level* CurrentLevel() const;                // the next level to play, by Stars()
  float LanternY(const Level* level) const;         // map y of a level's lantern or lamp, < 0 if none
  void Tap(glm::vec2 map);
  void Fill(CityFrame& frame, glm::vec2 screen, float scale);
  void DrawHud(wlw::ui::UIContext& ui);
  bool Button(wlw::ui::UIContext& ui, const wlw::ui::Rect& rect);  // a tap that started and ended in it

  ThreadlockApp& app_;
  const Level* focus_;
  std::optional<StarMap> dev_stars_;  // tl.stars
  city::MapLevels levels_;
  std::array<const Level*, city::kLanterns> lanterns_{};
  std::array<std::array<const Level*, city::kDistrictLamps>, 3> lamps_{};  // worlds 2-4
  CityFrame frame_;

  // The view, in map units.
  BoardCamera camera_;
  bool started_ = false;
  float view_ = 0.0f, scale_ = 1.0f;
  struct Glider {
    bool on = false;
    float from = 0.0f, to = 0.0f, t0 = 0.0f, seconds = 1.0f;
    std::function<float(float)> follow;  // instead of from -> to: the scroll at a time
  } glide_;

  // The animations: when each thing lit (clock_ seconds; kNever = not, kLongAgo = before).
  float clock_ = 0.0f;
  struct Event {
    float at;
    std::function<void()> run;
  };
  std::vector<Event> events_;
  float busy_until_ = 0.0f;
  std::array<std::array<float, 3>, city::kLanterns> lantern_at_{}, pane_at_{};
  std::array<std::array<std::array<float, 3>, city::kDistrictLamps>, 3> lamp_at_{};  // worlds 2-4, per star
  // The market's shop window: per level its bottle filled, corked, labelled, sealed in gold (a challenge: its globe).
  std::array<std::array<float, 4>, city::kDistrictLamps> bottle_at_{};
  float bell_at_ = kLongAgo;
  std::array<float, 15> hood_at_{};  // the canal quarter: when each harbour's neighbourhood began to light
  struct CanalPour {
    float from = 0.0f, to = 0.0f, t0 = 0.0f, seconds = 1.0f;  // at rest: from == to
  } canal_;
  std::array<bool, 3> gate_shown_{};
  float shop_tint_ = 0.0f;  // the top bar and the ambience follow the district in view: 1 = the market's
  std::array<float, 3> valve_at_{}, pour_at_{}, wake_at_{};
  int stars_shown_ = 0;
  int pulse_ = -1, pulse_world_ = 1;
  float pulse_until_ = 0.0f;
  int note_ = 0;

  // The finger.
  bool pressed_ = false, moved_ = false;
  glm::vec2 press_at_{0.0f};
  float last_y_ = 0.0f, velocity_ = 0.0f;
  float heading_ = 0.0f;
};

}  // namespace threadlock
