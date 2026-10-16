#pragma once

#include "threadlock_app.h"
#include "wlw/app/screen.h"

// Panels over the map: settings (sound, vibration, liquid colours), achievements, the daily puzzle
// (streak, the last week's archive) and the Lab (endless generated puzzles).
namespace threadlock {

class MenuScreen : public wlw::app::Screen {
 public:
  enum class Kind { Settings, Achievements, Daily, Lab };
  MenuScreen(ThreadlockApp& app, Kind kind) : app_(app), kind_(kind) {}

  bool IsOverlay() const override { return true; }
  void RenderUI(wlw::ui::UIContext& ui) override;
  bool OnBack() override;

 private:
  void Close();
  void Settings(wlw::ui::UIContext& ui);
  void Achievements(wlw::ui::UIContext& ui);
  void Daily(wlw::ui::UIContext& ui);
  void Lab(wlw::ui::UIContext& ui);

  ThreadlockApp& app_;
  Kind kind_;
  bool confirm_wipe_ = false;
};

}  // namespace threadlock
