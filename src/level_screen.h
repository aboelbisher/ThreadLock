#pragma once

#include <string_view>

#include <glm/glm.hpp>

#include "board_view.h"
#include "threadlock_app.h"
#include "world_look.h"
#include "wlw/app/screen.h"
#include "wlw/ui/icons.h"

// The level screen (DESIGN.md §8): the board in the middle, the level's name and star targets on
// top, the tray and the controls at the bottom. Place, then watch: drag pieces into sockets, tap
// one to change it, tap play, and the run plays out on its own. A failed run says what went wrong
// and where, and "Show me" replays the moment slowly under a spotlight.
namespace threadlock {

class LevelScreen : public wlw::app::Screen {
 public:
  LevelScreen(ThreadlockApp& app, const Level* level, LevelSource source);

  void OnEnter() override;
  void OnExit() override;
  void Update(float dt) override;
  void RenderUI(wlw::ui::UIContext& ui) override;
  bool OnBack() override;
  void OnAppPaused() override;

 private:
  enum class Phase { Edit, Run, Done, ShowMe };
  enum class Dialog { None, Result, Hints, Info, Menu };

  struct Drag {
    int piece = -1;
    int from = -1;  // the socket it came from, -1 = the tray
    glm::vec2 start{0.0f};
    glm::vec2 at{0.0f};
    bool moved = false;
    float held = 0.0f;
  };

  void Layout(const wlw::ui::UIContext& ui);
  void StartRun();
  void StopRun();
  void Step(bool quiet);
  void RunEnded();
  void StartShowMe();
  void Next();
  void LeaveToMap();
  bool SamePlacement(const Placement& a, const Placement& b) const;
  void ApplyHint();
  void ShowSolution();

  void HandleBoardInput(wlw::ui::UIContext& ui);
  void DrawTopBar(wlw::ui::UIContext& ui);
  void DrawBottom(wlw::ui::UIContext& ui);
  void DrawStatus(wlw::ui::UIContext& ui);
  void DrawResult(wlw::ui::UIContext& ui);
  void DrawHints(wlw::ui::UIContext& ui);
  void DrawInfo(wlw::ui::UIContext& ui);
  glm::vec2 TraySlot(int slot) const;
  int TraySlotOf(int piece) const;  // its place in the tray row (-1 if on the board)
  // The HUD's buttons in the world's colours: a round (or pill) one of dark glass, and the big action.
  bool HudButton(wlw::ui::UIContext& ui, const wlw::ui::Rect& rect, wlw::ui::Icon icon, std::string_view label = {});
  bool ActionButton(wlw::ui::UIContext& ui, const wlw::ui::Rect& rect, std::string_view label, wlw::ui::Icon icon,
                    bool quiet);

  ThreadlockApp& app_;
  const Level* level_;
  LevelSource source_;
  Board board_;
  const WorldLook& look_;  // the HUD's and the cards' colours
  BoardView view_;
  Phase phase_ = Phase::Edit;
  Dialog dialog_ = Dialog::None;
  float clock_ = 0.0f;
  int speed_ = 1;
  int runs_ = 0;  // runs on this visit (the ad rule: no interstitial after a hard-won solve)
  bool ran_once_ = false;
  float done_delay_ = 0.0f;
  bool ghosts_ = false;  // the solution is shown
  int hint_node_ = -1;
  float hint_time_ = 0.0f;
  int info_piece_ = -1;  // the piece whose card is open
  int show_from_ = 0, show_until_ = 0;
  bool show_done_ = false;
  Progress::Earned earned_;
  Outcome result_;
  Drag drag_;
  bool dragging_ = false;
  bool pressing_ = false;     // the pointer went down on something of ours
  bool dev_started_ = false;  // the dev shortcuts ran (tl.solve)
  float dev_clock_ = 0.0f;    // tl.autoplay

  // Layout (canvas units), set each frame.
  wlw::ui::Rect safe_;
  float top_ = 0.0f, bottom_ = 0.0f;
  wlw::ui::Rect board_rect_;
};

}  // namespace threadlock
