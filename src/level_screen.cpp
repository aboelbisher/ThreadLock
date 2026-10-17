#include "level_screen.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <tuple>
#include <vector>

#include "art_shapes.h"
#include "solver.h"
#include "ui_parts.h"
#include "world_look.h"
#include "wlw/core/cvar.h"
#include "wlw/core/log.h"
#include "wlw/ui/icons.h"
#include "wlw/ui/widgets.h"

namespace threadlock {
namespace {

using wlw::ui::Align;
using wlw::ui::Icon;
using wlw::ui::Rect;
using wlw::ui::UIContext;
using wlw::ui::Variant;

// Development shortcuts for screenshots (see threadlock_app.cpp).
wlw::CVar<bool> cv_solve{"tl.solve", false, "Place the level's Lean solution when it opens (dev)"};
wlw::CVar<float> cv_autoplay{"tl.autoplay", 0.0f, "Seconds after opening a level to tap play; 0 = never (dev)"};

constexpr float kLift = 56.0f;  // a dragged piece floats this far above the finger
constexpr float kLongPress = 0.45f;

struct PieceText {
  const char* text;
  const char* tap;
};

PieceText TextFor(const Piece& piece) {
  switch (piece.type) {
    case PieceType::Separator:
      return {
          "Sorts by colour: a blend splits into its two colours, each to its own side. A pure drop goes to its "
          "colour's side.",
          "Swap the sides"};
    case PieceType::Turnstile:
      return {"Lets one drop in at a time; the others wait. When two arrive together, its arrow picks who goes first.",
              "Turn the arrow"};
    case PieceType::TankShort: return {"Holds each drop 4 ticks longer.", "Nothing"};
    case PieceType::TankTall: return {"Holds each drop 8 ticks longer.", "Nothing"};
    case PieceType::Flipper:
      return {"Sends drops left, right, left, right, starting on the side it shows.", "Choose the first side"};
    case PieceType::Overflow:
      return {"The first drops go its way (the number on it). After that, everything goes the other way.",
              "Choose its way"};
    case PieceType::Bridge: return {"Lifts one lane over the other: no waiting and no mud.", "Nothing"};
    case PieceType::Mixer:
      return {"Waits for one drop from each way in, then mixes them: blue + amber = green, green + rose = pearl.",
              "Nothing"};
    case PieceType::TwinGate:
      return {"Holds two ways in until both have a drop, then lets the pair go together.", "Nothing"};
  }
  return {"", ""};
}

// "a, b and c"
std::string ListText(const std::vector<std::string>& items) {
  std::string text;
  for (size_t i = 0; i < items.size(); ++i) {
    if (i > 0) text += i + 1 == items.size() ? " and " : ", ";
    text += items[i];
  }
  return text;
}

// Where a piece does its job and where it's a trap, from the rules' table (DESIGN.md §3).
struct WhereText {
  std::string works;
  std::string trap;
};

WhereText WhereFor(PieceType type) {
  struct Place {
    NodeKind kind;
    bool twin;
    const char* name;
  };
  constexpr Place kPlaces[] = {{NodeKind::Fork, false, "forks"},
                               {NodeKind::Pass, false, "pipes"},
                               {NodeKind::Pass, true, "twin pipes"},
                               {NodeKind::Cross, false, "crossings"},
                               {NodeKind::Merge, false, "merges"}};
  std::vector<std::string> works, traps;
  for (const Place& place : kPlaces) {
    const PieceRole role = Role(type, place.kind, place.twin);
    // Twin pipes are pipes too: name them only when a piece does something else there.
    if (place.twin && role == Role(type, NodeKind::Pass, false)) continue;
    if (role == PieceRole::Acts) works.push_back(place.name);
    if (role == PieceRole::Trap) traps.push_back(place.name);
  }
  if (works.size() == 4) return {"every socket", ""};
  return {ListText(works), ListText(traps)};
}

// Splits `text` into lines no wider than `width` at `size`.
std::vector<std::string> Wrap(const wlw::ui::Font& font, const std::string& text, float size, float width) {
  std::vector<std::string> lines;
  std::string line;
  size_t i = 0;
  while (i < text.size()) {
    size_t end = text.find(' ', i);
    if (end == std::string::npos) end = text.size();
    const std::string word = text.substr(i, end - i);
    const std::string tried = line.empty() ? word : line + " " + word;
    if (!line.empty() && font.Measure(tried, size) > width) {
      lines.push_back(line);
      line = word;
    } else {
      line = tried;
    }
    i = end + 1;
  }
  if (!line.empty()) lines.push_back(line);
  return lines;
}

const char* FailureTitle(const Outcome& outcome) {
  switch (outcome.failure) {
    case Failure::Mud: return "Mud!";
    case Failure::WrongCup: return "Wrong colour";
    case Failure::Spill: return "Spill!";
    case Failure::Dry: return "A cup ran dry";
    case Failure::Jam: return "THREADLOCKED";
    case Failure::None: break;
  }
  return "Not quite";
}

std::string FailureText(const Level& level, const Board& board, const Outcome& outcome) {
  const Node* node = outcome.fail_node >= 0 ? &level.nodes[static_cast<size_t>(outcome.fail_node)] : nullptr;
  // A rack's bottle is named after its order when it has one.
  std::string bottle;
  if (node && node->kind == NodeKind::Rack && outcome.fail_slot >= 0) {
    const std::string& name = node->slots[static_cast<size_t>(outcome.fail_slot)].name;
    bottle = name.empty() ? "A bottle" : "The bottle for " + name;
  }
  switch (outcome.failure) {
    case Failure::Mud:
      return node && node->kind == NodeKind::Merge ? "Two colours met where the pipes merge."
                                                   : "Two colours were in the same junction at once.";
    case Failure::WrongCup:
      if (!bottle.empty()) return bottle + " got another colour.";
      return node ? std::string("The ") + ColorName(node->want) + " cup got another colour."
                  : "A cup got the wrong colour.";
    case Failure::Spill: return bottle.empty() ? "A cup got more than it holds." : bottle + " got more than it holds.";
    case Failure::Dry:
      return bottle.empty() ? "A cup ended short: its drops went elsewhere." : bottle + " ended short.";
    case Failure::Jam: {
      const int lonely = board.Jam().lonely;
      if (lonely < 0) return "Nothing can move: each lock waits for another.";
      if (level.nodes[static_cast<size_t>(lonely)].kind == NodeKind::Gate)
        return "A gate stays shut: no drop will turn its mill.";
      const int piece = board.PieceAt(lonely);
      if (piece >= 0 && board.RoleOf(piece) == PieceRole::Trap) {
        std::string name = PieceName(board.PieceInfo(piece).piece.type);
        name[0] = static_cast<char>(name[0] - 'A' + 'a');
        return "A " + name + " with one way in waits forever for a partner.";
      }
      return "A drop waits for a partner that never comes.";
    }
    case Failure::None: break;
  }
  return "";
}

}  // namespace

LevelScreen::LevelScreen(ThreadlockApp& app, const Level* level, LevelSource source)
    : app_(app), level_(level), source_(source), board_(*level), look_(LookForWorld(level->world)) {
  view_.SetArt(&app_.ArtFor(level_->world));
  view_.SetLevel(level_);
}

void LevelScreen::OnEnter() {
  app_.GetLiquid().SetActive(true);
  view_.Art().Invalidate();
  WLW_LOG_INFO("Level %s %s (%s)", level_->id.c_str(), level_->name.c_str(), LevelTypeKey(level_->type));
}

void LevelScreen::OnExit() {
  app_.Services().Window().GetScreen().SetClearColor(ui_color::kBackground);
  app_.GetProgress().Save();
}

void LevelScreen::OnAppPaused() { app_.GetProgress().Save(); }

bool LevelScreen::OnBack() {
  if (dialog_ != Dialog::None && dialog_ != Dialog::Result) {
    dialog_ = Dialog::None;
    return true;
  }
  if (phase_ == Phase::Run || phase_ == Phase::ShowMe) {
    StopRun();
    return true;
  }
  if (source_.kind == LevelSource::Main && !app_.GetProgress().MapOpen()) return false;  // first levels: no map yet
  LeaveToMap();
  return true;
}

// ---- Running ----

void LevelScreen::StartRun() {
  dragging_ = false;
  board_.Start();
  view_.ClearMarkers();
  phase_ = Phase::Run;
  clock_ = 0.0f;
  ++runs_;
  ran_once_ = true;
  hint_node_ = -1;
  app_.GetProgress().Count("runs");
}

void LevelScreen::StopRun() {
  board_.Stop();
  view_.ClearMarkers();
  phase_ = Phase::Edit;
  dialog_ = Dialog::None;
}

void LevelScreen::Step(bool quiet) {
  const std::vector<Event>& events = board_.Tick();
  if (quiet) return;
  view_.OnEvents(events, board_);
  app_.GetFeedback().OnEvents(events, *level_, board_);
  if (phase_ != Phase::Run) return;
  Progress& progress = app_.GetProgress();
  for (const Event& e : events) {
    if (e.type == EventType::NearMiss) progress.Count("near_misses");
    if (e.type == EventType::Collide) progress.Count("mud");
    if (e.type == EventType::Jammed) progress.Count("jams");
  }
}

bool LevelScreen::SamePlacement(const Placement& a, const Placement& b) const {
  auto canonical = [&](const Placement& p) {
    std::vector<std::tuple<int, int, int, int>> items;
    for (size_t i = 0; i < p.at.size() && i < level_->tray.size(); ++i) {
      if (p.at[i] < 0 || level_->tray[i].locked) continue;
      const Piece& piece = level_->tray[i].piece;
      const int setting = HasSetting(piece.type) && i < p.setting.size() ? p.setting[i] : 0;
      items.emplace_back(static_cast<int>(piece.type), piece.n, p.at[i], setting);
    }
    std::sort(items.begin(), items.end());
    return items;
  };
  return canonical(a) == canonical(b);
}

void LevelScreen::RunEnded() {
  result_ = board_.GetOutcome();
  phase_ = Phase::Done;
  Progress& progress = app_.GetProgress();
  earned_ = {};
  if (result_.solved) {
    done_delay_ = look_.art == ArtKind::Lantern ? 2.0f : 1.3f;  // time to watch the window light up
    app_.GetFeedback().Play(synth::Sound::Fanfare, 0.7f);
    const Placement placement = board_.GetPlacement();
    const bool shown = progress.SolutionShown(level_->id) &&
                       (SamePlacement(placement, level_->pars.lean) || SamePlacement(placement, level_->pars.swift));
    if (source_.kind == LevelSource::Main) {
      const int stars_before = progress.StarsBefore(app_.GetCatalog(), level_->world + 1);
      earned_ = progress.RecordSolve(*level_, result_.pieces, result_.ticks, shown);
      const int next_world = level_->world + 1;
      if (app_.GetCatalog().GetWorld(next_world) && stars_before < Catalog::Gate(next_world) &&
          progress.WorldOpen(app_.GetCatalog(), next_world)) {
        app_.Toast("World " + std::to_string(next_world) + " is open!");
      }
    } else {
      earned_.bits = kSolveStar;
      if (level_->pars.Known()) {
        if (result_.pieces <= level_->pars.pieces) earned_.bits |= kLeanStar;
        if (result_.ticks <= level_->pars.ticks) earned_.bits |= kSwiftStar;
      }
      earned_.fresh = earned_.bits;
      if (source_.kind == LevelSource::Daily) {
        const int today = app_.Today();
        if (!source_.archive) {
          if (progress.MarkDaily(source_.date)) {
            progress.AddHints(1);
            app_.Toast("Daily solved: +1 hint");
          }
        } else if (progress.CanMendStreak(today) && !progress.Daily().Marked(source_.date)) {
          progress.MendStreak(source_.date, today);
          app_.Toast("Streak mended");
        }
        progress.Count("daily_solved");
      } else {
        progress.Count("lab_solved");
      }
    }
    progress.Count("solves");
    app_.LevelSolved();
    for (const std::string& key : progress.UpdateAchievements(app_.GetCatalog())) {
      for (const Achievement& achievement : kAchievements) {
        if (key == achievement.key) app_.Toast(std::string("Achievement: ") + achievement.name);
      }
    }
    progress.Save();
  } else {
    done_delay_ = result_.jammed ? 1.6f : 1.0f;
    progress.Count("fails");
    for (const std::string& key : progress.UpdateAchievements(app_.GetCatalog())) {  // Threadlocked: the first jam
      for (const Achievement& achievement : kAchievements) {
        if (key == achievement.key) app_.Toast(std::string("Achievement: ") + achievement.name);
      }
    }
  }
}

void LevelScreen::StartShowMe() {
  dialog_ = Dialog::None;
  const int fail_tick = std::max(1, result_.fail_tick);
  show_from_ = std::max(0, fail_tick - 12);
  show_until_ = fail_tick + 8;
  board_.Start();
  view_.ClearMarkers();
  while (board_.Running() && board_.Ticks() < show_from_) Step(true);
  phase_ = Phase::ShowMe;
  clock_ = 0.0f;
  show_done_ = false;
}

void LevelScreen::Update(float dt) {
  if (!dev_started_) {
    dev_started_ = true;
    if (cv_solve.Get() && level_->pars.Known()) board_.SetPlacement(level_->pars.lean);
  }
  if (cv_autoplay.Get() > 0.0f && !ran_once_ && phase_ == Phase::Edit) {
    dev_clock_ += dt;
    if (dev_clock_ >= cv_autoplay.Get()) StartRun();
  }
  view_.Update(dt, board_);
  hint_time_ = std::max(0.0f, hint_time_ - dt);
  if (hint_time_ <= 0.0f) hint_node_ = -1;
  switch (phase_) {
    case Phase::Run:
      clock_ += dt * static_cast<float>(speed_);
      while (clock_ >= kTickSeconds && board_.Running()) {
        clock_ -= kTickSeconds;
        Step(false);
      }
      if (!board_.Running()) {
        clock_ = 0.0f;
        RunEnded();
      }
      break;
    case Phase::ShowMe:
      if (show_done_) break;
      clock_ += dt * 0.25f;
      while (clock_ >= kTickSeconds && board_.Running() && board_.Ticks() < show_until_) {
        clock_ -= kTickSeconds;
        Step(false);
      }
      if (!board_.Running() || board_.Ticks() >= show_until_) {
        show_done_ = true;
        clock_ = 0.0f;
      }
      break;
    case Phase::Done:
      if (done_delay_ > 0.0f) {
        done_delay_ -= dt;
        if (done_delay_ <= 0.0f) dialog_ = Dialog::Result;
      }
      break;
    case Phase::Edit: break;
  }
}

// ---- Leaving ----

void LevelScreen::Next() {
  dialog_ = Dialog::None;
  ThreadlockApp& app = app_;
  const bool hard_won = runs_ >= 5;
  switch (source_.kind) {
    case LevelSource::Main: {
      const Level* next = app.GetCatalog().NextOnPath(*level_);
      const bool allowed = !app.GetCatalog().IsFinale(*level_) && !hard_won;
      const Level* here = level_;
      app.BetweenLevels(allowed, [&app, next, here] {
        if (next && app.GetProgress().LevelOpen(app.GetCatalog(), *next)) {
          app.OpenLevel(next);
        } else {
          app.OpenMap(here);
        }
      });
      break;
    }
    case LevelSource::Lab: app.BetweenLevels(!hard_won, [&app] { app.OpenLab(); }); break;
    case LevelSource::Daily: app.OpenMap(); break;
  }
}

void LevelScreen::LeaveToMap() {
  ThreadlockApp& app = app_;
  const Level* here = source_.kind == LevelSource::Main ? level_ : nullptr;
  const bool solved = phase_ == Phase::Done && result_.solved;
  const bool allowed = solved && source_.kind == LevelSource::Main && !app.GetCatalog().IsFinale(*level_) && runs_ < 5;
  if (solved) {
    app.BetweenLevels(allowed, [&app, here] { app.OpenMap(here); });
  } else {
    app.OpenMap(here);
  }
}

// ---- Hints ----

void LevelScreen::ApplyHint() {
  if (!level_->pars.Known()) return;
  const HintMove move = NextHint(*level_, board_.GetPlacement(), level_->pars.lean);
  if (move.piece >= 0 && move.node >= 0) {
    board_.Place(move.piece, move.node);
    board_.SetSetting(move.piece, move.setting);
    hint_node_ = move.node;
  } else if (move.piece >= 0) {
    hint_node_ = board_.PieceNode(move.piece);
    board_.Unplace(move.piece);
  } else if (move.flap_node >= 0) {
    board_.SetFlap(move.flap_node, move.flap);
    hint_node_ = move.flap_node;
  }
  hint_time_ = 2.5f;
  app_.GetFeedback().Play(synth::Sound::Place, 0.8f);
}

void LevelScreen::ShowSolution() {
  ghosts_ = true;
  if (source_.kind == LevelSource::Main) app_.GetProgress().MarkSolutionShown(level_->id);
  app_.GetProgress().Save();
}

// ---- Layout and drawing ----

void LevelScreen::Layout(const UIContext& ui) {
  safe_ = ui.safe_area;
  top_ = safe_.min.y;
  bottom_ = safe_.max.y;
  board_rect_ = {{safe_.min.x + 4.0f, top_ + 64.0f}, {safe_.max.x - 4.0f, bottom_ - 176.0f}};
  const glm::vec2 size = board_rect_.Size();
  const float scale = std::max(0.3f, std::min(size.x / kBoardSize.x, size.y / kBoardSize.y));
  const glm::vec2 origin = board_rect_.Center() - kBoardSize * (scale * 0.5f);
  view_.SetLayout({origin, scale});
}

glm::vec2 LevelScreen::TraySlot(int slot) const {
  int count = 0;
  for (const TrayPiece& piece : level_->tray) count += piece.locked ? 0 : 1;
  const float x =
      (safe_.min.x + safe_.max.x) * 0.5f + (static_cast<float>(slot) - static_cast<float>(count - 1) * 0.5f) * 64.0f;
  return {x, bottom_ - 114.0f};
}

int LevelScreen::TraySlotOf(int piece) const {
  int slot = 0;
  for (int i = 0; i < board_.PieceCount(); ++i) {
    if (board_.Locked(i)) continue;
    if (i == piece) return slot;
    ++slot;
  }
  return -1;
}

void LevelScreen::RenderUI(UIContext& ui) {
  Layout(ui);
  const Palette& palette = app_.GetPalette();
  const wlw::ui::Font& font = app_.GetFont();
  wlw::ui::Canvas& canvas = ui.canvas;
  const float time = app_.Time();

  // The world's colours for the HUD and the cards (the same layout in every world).
  app_.Services().Window().GetScreen().SetClearColor(look_.background);
  ui.theme.primary = look_.action_bottom;
  ui.theme.on_primary = look_.on_action;
  ui.theme.secondary = Mixed(look_.background, look_.ink, 0.16f);
  ui.theme.on_secondary = look_.ink;
  const Rgba panel{look_.panel.r, look_.panel.g, look_.panel.b, 1.0f};
  ui.theme.surface = WithAlpha(Mixed(panel, look_.ink, 0.04f), 0.98f);
  ui.theme.surface_raised = Mixed(panel, look_.ink, 0.1f);
  ui.theme.track = look_.background;
  ui.theme.text = look_.ink;
  ui.theme.text_muted = look_.muted;
  ui.theme.on = look_.accent;

  // The place and the machine on the bloomed layer, the liquid over them, then the parts' fronts.
  view_.BeginFrame(board_, palette, font, time, ui.dt, app_.Calm(), phase_ == Phase::Edit);
  Liquid& liquid = app_.GetLiquid();
  view_.DrawBack(liquid.Picture());
  liquid.Begin(canvas.Size(), time);
  const float alpha = phase_ == Phase::Run || (phase_ == Phase::ShowMe && !show_done_) ? clock_ / kTickSeconds : 1.0f;
  view_.DrawLiquid(liquid, alpha);
  liquid.End();
  liquid.Draw(liquid.Picture());

  if (dialog_ == Dialog::None) HandleBoardInput(ui);

  BoardView::Options options;
  if (dragging_ && drag_.moved) {
    options.drag_type = static_cast<int>(board_.PieceInfo(drag_.piece).piece.type);
    options.drag_target = view_.SocketNear(drag_.at - glm::vec2(0.0f, kLift), 48.0f);
  }
  options.hint_node = hint_node_;
  options.ghosts = ghosts_ && phase_ == Phase::Edit;
  options.ghost_placement = &level_->pars.lean;
  view_.DrawParts(canvas, options);
  view_.DrawMarkers(canvas);
  if (phase_ == Phase::ShowMe) view_.DrawSpotlight(canvas, result_.fail_node, 1.0f);

  DrawTopBar(ui);
  DrawStatus(ui);
  DrawBottom(ui);

  if (dragging_ && drag_.moved) {
    PieceLook look;
    look.setting = board_.Setting(drag_.piece);
    look.accent = look_.accent;
    DrawPiece(canvas, board_.PieceInfo(drag_.piece).piece, drag_.at - glm::vec2(0.0f, kLift), view_.GetLayout().scale,
              look, palette, nullptr, nullptr, &font);
  }

  switch (dialog_) {
    case Dialog::Result: DrawResult(ui); break;
    case Dialog::Hints: DrawHints(ui); break;
    case Dialog::Info: DrawInfo(ui); break;
    default: break;
  }
}

void LevelScreen::HandleBoardInput(UIContext& ui) {
  const glm::vec2 p = ui.pointer.position;
  if (!ui.interactive) {  // a fade, or something over this screen
    dragging_ = false;
    return;
  }
  if (phase_ != Phase::Edit) {
    dragging_ = false;
    return;
  }
  if (ui.pointer.pressed && !dragging_) {
    int piece = -1, from = -1;
    for (int i = 0; i < board_.PieceCount() && piece < 0; ++i) {
      if (board_.Locked(i) || board_.PieceNode(i) >= 0) continue;
      if (glm::distance(p, TraySlot(TraySlotOf(i))) < 30.0f) piece = i;
    }
    if (piece < 0) {
      piece = view_.PieceNear(board_, p, 30.0f * std::max(1.0f, view_.GetLayout().scale));
      if (piece >= 0) from = board_.PieceNode(piece);
    }
    if (piece >= 0) {
      drag_ = {piece, from, p, p, false, 0.0f};
      dragging_ = true;
    } else {
      const int flap = view_.FlapNear(board_, p, 24.0f);
      if (flap >= 0) {
        board_.SetFlap(flap, 1 - board_.Flap(flap));
        app_.GetFeedback().Play(synth::Sound::Tap);
      }
    }
  }
  if (!dragging_) return;
  drag_.at = p;
  drag_.held += ui.dt;
  if (!drag_.moved && glm::distance(drag_.at, drag_.start) > 10.0f) {
    if (board_.Locked(drag_.piece)) {
      dragging_ = false;  // locked pieces stay
      return;
    }
    drag_.moved = true;
    if (drag_.from >= 0) board_.Unplace(drag_.piece);
    app_.GetFeedback().Play(synth::Sound::Lift);
  }
  if (!drag_.moved && drag_.held > kLongPress && ui.pointer.down) {
    info_piece_ = drag_.piece;
    dialog_ = Dialog::Info;
    dragging_ = false;
    return;
  }
  if (!ui.pointer.released && ui.pointer.down) return;
  dragging_ = false;
  if (!drag_.moved) {
    // A tap: a placed piece changes its setting; a tray piece shows its card.
    if (drag_.from >= 0) {
      const PieceType type = board_.PieceInfo(drag_.piece).piece.type;
      if (HasSetting(type) && !board_.Locked(drag_.piece)) {
        board_.SetSetting(drag_.piece, 1 - board_.Setting(drag_.piece));
        app_.GetFeedback().Play(synth::Sound::Tap);
      } else {
        info_piece_ = drag_.piece;
        dialog_ = Dialog::Info;
      }
    } else {
      info_piece_ = drag_.piece;
      dialog_ = Dialog::Info;
    }
    return;
  }
  const int target = view_.SocketNear(drag_.at - glm::vec2(0.0f, kLift), 48.0f);
  if (target >= 0 && board_.Place(drag_.piece, target)) {
    app_.GetFeedback().Play(synth::Sound::Place);
    app_.GetFeedback().Vibrate(10, 0.35f);
  } else {
    board_.Unplace(drag_.piece);
    app_.GetFeedback().Play(synth::Sound::Lift, 0.6f, 0.7f);
  }
}

bool LevelScreen::HudButton(UIContext& ui, const Rect& rect, Icon icon, std::string_view label) {
  wlw::ui::Canvas& canvas = ui.canvas;
  const std::string_view key = label.empty() ? std::string_view(icon == Icon::Hint ? "hud-hint" : "hud-icon") : label;
  const wlw::ui::Press press = wlw::ui::Pressable(ui, wlw::ui::WidgetId(key, rect), rect);
  const float r = std::min(rect.Size().x, rect.Size().y) * 0.5f;
  const glm::vec2 sink{0.0f, 1.5f * press.amount};
  const Rect face{rect.min + sink, rect.max + sink};
  const Rgba panel{look_.panel.r, look_.panel.g, look_.panel.b, 1.0f};
  canvas.FillRect({face.min + glm::vec2(0.0f, 3.0f), face.max + glm::vec2(0.0f, 3.0f)}, Rgba{0.0f, 0.0f, 0.0f, 0.35f},
                  r);
  canvas.FillRect(face, Mixed(panel, look_.panel_edge, look_.panel_edge.a * 1.3f), r);
  canvas.FillRect({face.min + glm::vec2(1.2f), face.max - glm::vec2(1.2f)},
                  press.held ? Mixed(panel, look_.ink, 0.08f) : panel, r - 1.2f);
  if (!label.empty()) {
    canvas.Text(app_.GetFont(), label, face.Center() + glm::vec2(0.0f, 1.0f), 19.0f, Align::Center,
                {.color = look_.ink});
  } else {
    wlw::ui::DrawIcon(canvas, icon, {face.Center() - glm::vec2(11.0f), face.Center() + glm::vec2(11.0f)}, look_.ink);
  }
  return press.clicked;
}

bool LevelScreen::ActionButton(UIContext& ui, const Rect& rect, std::string_view label, Icon icon, bool quiet) {
  if (quiet) return HudButton(ui, rect, Icon::None, label);
  wlw::ui::Canvas& canvas = ui.canvas;
  const wlw::ui::Press press = wlw::ui::Pressable(ui, wlw::ui::WidgetId(label, rect), rect);
  const float r = rect.Size().y * 0.5f;
  const float depth = 4.0f;
  const glm::vec2 sink{0.0f, depth * 0.75f * press.amount};
  const Rect face{rect.min + sink, rect.max + sink - glm::vec2(0.0f, depth)};
  canvas.FillRect({rect.min + glm::vec2(0.0f, 5.0f), rect.max + glm::vec2(0.0f, 3.0f)}, Rgba{0.0f, 0.0f, 0.0f, 0.35f},
                  r);
  canvas.FillRect({{rect.min.x, rect.min.y + depth}, rect.max}, look_.action_lip, r);
  if (const auto& material = app_.ActionMaterial()) {
    material->SetParam("uTop", glm::vec4(look_.action_top.r, look_.action_top.g, look_.action_top.b, 1.0f));
    material->SetParam("uBottom", glm::vec4(look_.action_bottom.r, look_.action_bottom.g, look_.action_bottom.b, 1.0f));
    material->SetParam("uRadius", (face.Size().y * 0.5f) * canvas.PixelsPerUnit());
    canvas.ShaderRect(face, material);
  }
  const wlw::ui::Font& font = app_.GetFont();
  const float size = 21.0f;
  const float text_width = font.Measure(label, size);
  const float icon_size = 20.0f, gap = 8.0f;
  const float x = face.Center().x - (text_width + icon_size + gap) * 0.5f;
  const glm::vec2 centre{x + icon_size * 0.5f, face.Center().y};
  wlw::ui::DrawIcon(canvas, icon, {centre - glm::vec2(icon_size * 0.5f), centre + glm::vec2(icon_size * 0.5f)},
                    look_.on_action);
  canvas.Text(font, label, {x + icon_size + gap, face.Center().y + 1.0f}, size, Align::Left,
              {.color = look_.on_action});
  return press.clicked;
}

void LevelScreen::DrawTopBar(UIContext& ui) {
  wlw::ui::Canvas& canvas = ui.canvas;
  const wlw::ui::Font& font = app_.GetFont();
  const float y = top_ + 30.0f;
  const bool can_leave = source_.kind != LevelSource::Main || app_.GetProgress().MapOpen();
  float x = safe_.min.x + 14.0f;
  if (can_leave) {
    if (HudButton(ui, {{x, y - 21.0f}, {x + 42.0f, y + 21.0f}}, Icon::Back) && dialog_ == Dialog::None) {
      app_.GetFeedback().Play(synth::Sound::Tap);
      if (phase_ == Phase::Run) StopRun();
      LeaveToMap();
    }
    x += 54.0f;
  }
  const float title_width = safe_.max.x - 160.0f - x;
  canvas.Text(font, level_->name, {x, y - 8.0f}, wlw::ui::FitTextSize(font, level_->name, 25.0f, title_width),
              Align::Left, Outlined(look_.ink));
  // "1-6 · First Light" (the font has no middle dot: it's drawn).
  std::string code = level_->id;
  std::string place;
  if (source_.kind == LevelSource::Daily) code = source_.archive ? "Daily (archive)" : "Daily";
  if (source_.kind == LevelSource::Lab) code = "Lab";
  if (source_.kind == LevelSource::Main) {
    if (const World* world = app_.GetCatalog().GetWorld(level_->world)) place = world->name;
    if (level_->type == LevelType::Repair) place += ", repair";
    if (level_->type == LevelType::Challenge) place = "challenge";
  }
  const float sub = 13.0f;
  canvas.Text(font, code, {x, y + 15.0f}, sub, Align::Left, Outlined(look_.accent));
  if (!place.empty()) {
    const float after = x + font.Measure(code, sub);
    canvas.Circle({after + 7.0f, y + 14.0f}, 1.6f, look_.accent);
    canvas.Text(font, place, {after + 14.0f, y + 15.0f}, sub, Align::Left, Outlined(look_.accent));
  }

  // The three star targets: Solve, Lean (pieces), Swift (time).
  const int bits = source_.kind == LevelSource::Main ? app_.GetProgress().Bits(level_->id) : 0;
  const char* labels[3] = {"solve", nullptr, nullptr};
  std::string lean = level_->pars.Known() ? std::to_string(level_->pars.pieces) + " pcs" : "-";
  std::string swift = level_->pars.Known() ? TimeText(level_->pars.ticks) : "-";
  labels[1] = lean.c_str();
  labels[2] = swift.c_str();
  const int masks[3] = {kSolveStar, kLeanStar, kSwiftStar};
  for (int i = 0; i < 3; ++i) {
    const glm::vec2 c{safe_.max.x - 126.0f + 50.0f * static_cast<float>(i), y - 7.0f};
    const bool have = bits & masks[i];
    if (have) {
      StarShape(canvas, c, 11.5f, ui_color::kStar);
    } else {
      StarShape(canvas, c, 11.5f, Mixed(look_.background, look_.muted, 0.45f));
      StarShape(canvas, c, 9.0f, Mixed(look_.background, look_.ink, 0.14f));
    }
    canvas.Text(font, labels[i], c + glm::vec2(0.0f, 20.0f), 11.0f, Align::Center,
                Outlined(have ? ui_color::kStar : look_.muted));
  }
}

void LevelScreen::DrawStatus(UIContext& ui) {
  wlw::ui::Canvas& canvas = ui.canvas;
  const wlw::ui::Font& font = app_.GetFont();
  const glm::vec2 at{(safe_.min.x + safe_.max.x) * 0.5f, bottom_ - 164.0f};
  std::string text;
  Rgba color = look_.ink;
  const Outcome& outcome = board_.GetOutcome();
  switch (phase_) {
    case Phase::Edit:
      if (!ran_once_ && !level_->intro.empty()) {
        text = level_->intro;
      } else if (board_.PlacedCount() == 0 && !level_->tray.empty()) {
        text = "Drag pieces into the sockets";
        color = look_.muted;
      } else {
        text = "Ready? Tap play";
      }
      break;
    case Phase::Run:
      if (outcome.failure != Failure::None) {
        text = FailureTitle(outcome);
        color = ui_color::kBad;
      } else {
        text = "Pouring...  " + TimeText(board_.Ticks());
        color = look_.muted;
      }
      break;
    case Phase::Done:
      text = outcome.solved ? "Every lantern lit" : FailureTitle(outcome);
      if (outcome.solved && look_.art != ArtKind::Lantern) text = "Every cup full and pure";
      color = outcome.solved ? look_.accent : ui_color::kBad;
      break;
    case Phase::ShowMe:
      text = show_done_ ? FailureTitle(result_) : "Watch closely...";
      color = show_done_ ? ui_color::kBad : look_.muted;
      break;
  }
  const float size = wlw::ui::FitTextSize(font, text, 16.0f, safe_.Size().x - 24.0f);
  canvas.Text(font, text, at, size, Align::Center, Outlined(color));
}

void LevelScreen::DrawBottom(UIContext& ui) {
  wlw::ui::Canvas& canvas = ui.canvas;
  const wlw::ui::Font& font = app_.GetFont();
  const Palette& palette = app_.GetPalette();
  const float left = safe_.min.x, right = safe_.max.x;
  const bool editing = phase_ == Phase::Edit && dialog_ == Dialog::None;

  // The tray: every movable piece has its slot; one on the board leaves an empty ring.
  const Rect tray{{left + 10.0f, bottom_ - 146.0f}, {right - 10.0f, bottom_ - 82.0f}};
  const Rgba edge = Mixed(Rgba{look_.panel.r, look_.panel.g, look_.panel.b, 1.0f}, look_.panel_edge, 0.8f);
  canvas.FillRect({tray.min - glm::vec2(1.2f), tray.max + glm::vec2(1.2f)}, WithAlpha(edge, look_.panel.a), 21.0f);
  canvas.FillRect(tray, look_.panel, 20.0f);
  canvas.Line({tray.min.x + 22.0f, tray.min.y + 2.0f}, {tray.max.x - 22.0f, tray.min.y + 2.0f}, 1.2f,
              WithAlpha(Hex(0xFFFFFF), 0.1f));
  bool any = false;
  for (int i = 0; i < board_.PieceCount(); ++i) {
    if (board_.Locked(i)) continue;
    any = true;
    const glm::vec2 slot = TraySlot(TraySlotOf(i));
    if (board_.PieceNode(i) >= 0 || (dragging_ && drag_.piece == i && drag_.moved)) {
      canvas.Ring(slot, 19.0f, 1.5f, WithAlpha(look_.ink, 0.15f));
      continue;
    }
    PieceLook look;
    look.setting = board_.Setting(i);
    look.alpha = editing ? 1.0f : 0.4f;
    look.accent = look_.accent;
    DrawPiece(canvas, board_.PieceInfo(i).piece, slot, 0.78f, look, palette, nullptr, nullptr, &font);
  }
  if (!any) {
    canvas.Text(font, "No pieces: just watch", tray.Center() + glm::vec2(0.0f, 1.0f), 16.0f, Align::Center,
                Outlined(look_.muted));
  }

  // Controls: hint, restart, speed, and the big play button.
  const float y = bottom_ - 40.0f;
  const bool can_click = dialog_ == Dialog::None;
  const Rect hint{{left + 12.0f, y - 24.0f}, {left + 60.0f, y + 24.0f}};
  if (HudButton(ui, hint, Icon::Hint) && can_click) {
    app_.GetFeedback().Play(synth::Sound::Tap);
    dialog_ = Dialog::Hints;
  }
  const int hints = app_.GetProgress().Hints();
  if (source_.kind != LevelSource::Lab) {
    const glm::vec2 badge{hint.max.x - 3.0f, hint.min.y + 3.0f};
    canvas.Circle(badge, 10.0f, ui_color::kStar);
    canvas.Text(font, std::to_string(hints), badge + glm::vec2(0.0f, 0.5f), 13.0f, Align::Center,
                {.color = Hex(0x2A1A00)});
  }
  if (HudButton(ui, {{left + 70.0f, y - 24.0f}, {left + 118.0f, y + 24.0f}}, Icon::Restart) && can_click) {
    app_.GetFeedback().Play(synth::Sound::Tap);
    if (phase_ != Phase::Edit) StopRun();
    board_.ClearPieces();
  }
  const std::string speed = std::to_string(speed_) + "x";
  if (HudButton(ui, {{left + 128.0f, y - 24.0f}, {left + 184.0f, y + 24.0f}}, Icon::None, speed) && can_click) {
    speed_ = speed_ == 4 ? 1 : speed_ * 2;
    app_.GetFeedback().Play(synth::Sound::Tap);
  }

  const Rect play{{left + 194.0f, y - 26.0f}, {right - 12.0f, y + 28.0f}};
  const char* label = "PLAY";
  Icon icon = Icon::Play;
  if (phase_ == Phase::Run) label = "STOP", icon = Icon::Pause;
  if (phase_ == Phase::Done || phase_ == Phase::ShowMe) label = "EDIT", icon = Icon::Undo;
  const bool pulse =
      phase_ == Phase::Edit && !ran_once_ && !app_.Calm() && (level_->tray.empty() || board_.PlacedCount() > 0);
  if (pulse)
    canvas.FillRect({play.min - glm::vec2(4.0f), play.max + glm::vec2(4.0f)},
                    WithAlpha(look_.action_top, 0.22f + 0.18f * std::sin(app_.Time() * 5.0f)), 31.0f);
  if (ActionButton(ui, play, label, icon, phase_ == Phase::Run) && can_click) {
    app_.GetFeedback().Play(synth::Sound::Tap);
    if (phase_ == Phase::Edit) {
      StartRun();
    } else {
      StopRun();
    }
  }

  // Show me: replay controls once the moment has passed.
  if (phase_ == Phase::ShowMe && show_done_) {
    const glm::vec2 c = tray.Center();
    canvas.FillRect(tray, Rgba{look_.panel.r, look_.panel.g, look_.panel.b, 1.0f}, 20.0f);
    if (wlw::ui::Button(ui, "Show again", {{c.x - 150.0f, c.y - 22.0f}, {c.x - 6.0f, c.y + 22.0f}},
                        Variant::Secondary)) {
      StartShowMe();
    }
    if (wlw::ui::Button(ui, "Edit", {{c.x + 6.0f, c.y - 22.0f}, {c.x + 150.0f, c.y + 22.0f}}, Variant::Primary))
      StopRun();
  }
}

void LevelScreen::DrawResult(UIContext& ui) {
  wlw::ui::Canvas& canvas = ui.canvas;
  const wlw::ui::Font& font = app_.GetFont();
  if (!result_.solved) {
    const Rgba primary = ui.theme.primary;
    ui.theme.primary = ui_color::kBad;  // the title takes the primary colour: a failure isn't green
    const wlw::ui::DialogResult dialog = wlw::ui::Dialog(ui, FailureTitle(result_), {310.0f, 250.0f}, true);
    ui.theme.primary = primary;
    const Rect& r = dialog.content;
    const std::vector<std::string> lines = Wrap(font, FailureText(*level_, board_, result_), 17.0f, r.Size().x - 20.0f);
    float y = r.min.y + 18.0f;
    for (const std::string& line : lines) {
      canvas.Text(font, line, {r.Center().x, y}, 17.0f, Align::Center, Outlined(ui_color::kInk));
      y += 22.0f;
    }
    const float by = r.max.y - 34.0f;
    if (wlw::ui::Button(ui, "Show me", {{r.min.x + 10.0f, by - 50.0f}, {r.max.x - 10.0f, by - 6.0f}}, Variant::Primary,
                        Icon::Play)) {
      StartShowMe();
    }
    if (wlw::ui::Button(ui, "Edit", {{r.min.x + 10.0f, by}, {r.max.x - 10.0f, by + 30.0f}}, Variant::Ghost) ||
        dialog.close) {
      StopRun();
    }
    return;
  }
  const wlw::ui::DialogResult dialog = wlw::ui::Dialog(ui, "Solved!", {320.0f, 330.0f}, false);
  const Rect& r = dialog.content;
  const int before = source_.kind == LevelSource::Main ? app_.GetProgress().Bits(level_->id) & ~earned_.fresh : 0;
  const int masks[3] = {kSolveStar, kLeanStar, kSwiftStar};
  const std::string labels[3] = {"Solved",
                                 level_->pars.Known() ? "Lean: " + std::to_string(level_->pars.pieces) +
                                                            (level_->pars.pieces == 1 ? " piece" : " pieces")
                                                      : "",
                                 level_->pars.Known() ? "Swift: " + TimeText(level_->pars.ticks) : ""};
  for (int i = 0; i < 3; ++i) {
    const glm::vec2 c{r.Center().x + (static_cast<float>(i) - 1.0f) * 88.0f, r.min.y + 34.0f};
    const bool now = earned_.bits & masks[i];
    const bool old = before & masks[i];
    const float pop = app_.Calm() ? 1.0f : 1.0f + 0.15f * std::sin(app_.Time() * 4.0f + static_cast<float>(i));
    StarShape(canvas, c, (now ? 24.0f * pop : 21.0f),
              now   ? ui_color::kStar
              : old ? WithAlpha(ui_color::kStar, 0.35f)
                    : WithAlpha(ui_color::kFaint, 0.5f));
    canvas.Text(font, labels[i], c + glm::vec2(0.0f, 36.0f), 14.0f, Align::Center,
                Outlined(now ? ui_color::kStar : ui_color::kMuted));
  }
  std::string mine = "Your machine: " + std::to_string(result_.pieces) + (result_.pieces == 1 ? " piece" : " pieces") +
                     ", " + TimeText(result_.ticks);
  canvas.Text(font, mine, {r.Center().x, r.min.y + 104.0f}, 15.0f, Align::Center, Outlined(ui_color::kInk));
  std::string note;
  if (earned_.bits & kGoldFrame)
    note = "Lean and Swift in one run!";
  else if (!(earned_.bits & kLeanStar) && level_->pars.Known())
    note = "Can it work with fewer pieces?";
  else if (!(earned_.bits & kSwiftStar) && level_->pars.Known())
    note = "Locks are safe, but timing is faster.";
  if (ghosts_ && !(earned_.bits & kLeanStar)) note = "Lean and Swift need a machine of your own.";
  canvas.Text(font, note, {r.Center().x, r.min.y + 128.0f}, 14.0f, Align::Center, Outlined(ui_color::kMuted));

  const float by = r.max.y - 58.0f;
  const char* next_label = source_.kind == LevelSource::Lab     ? "Next puzzle"
                           : source_.kind == LevelSource::Daily ? "Done"
                                                                : "Next";
  if (wlw::ui::Button(ui, next_label, {{r.min.x + 10.0f, by - 52.0f}, {r.max.x - 10.0f, by - 6.0f}}, Variant::Primary,
                      Icon::Next)) {
    Next();
    return;
  }
  const float mid = r.Center().x;
  if (wlw::ui::Button(ui, "Replay", {{r.min.x + 10.0f, by + 2.0f}, {mid - 5.0f, by + 44.0f}}, Variant::Secondary,
                      Icon::Restart)) {
    dialog_ = Dialog::None;
    StartRun();
    return;
  }
  if (wlw::ui::Button(ui, "Edit", {{mid + 5.0f, by + 2.0f}, {r.max.x - 10.0f, by + 44.0f}}, Variant::Secondary,
                      Icon::Undo)) {
    StopRun();
  }
}

void LevelScreen::DrawHints(UIContext& ui) {
  wlw::ui::Canvas& canvas = ui.canvas;
  const wlw::ui::Font& font = app_.GetFont();
  Progress& progress = app_.GetProgress();
  const bool lab = source_.kind == LevelSource::Lab;
  const wlw::ui::DialogResult dialog = wlw::ui::Dialog(ui, "Hints", {310.0f, 400.0f}, true);
  if (dialog.close) {
    dialog_ = Dialog::None;
    return;
  }
  const Rect& r = dialog.content;
  const int hints = progress.Hints();
  const std::string have =
      lab ? "Lab hints come from ads." : "You have " + std::to_string(hints) + (hints == 1 ? " hint." : " hints.");
  canvas.Text(font, have, {r.Center().x, r.min.y + 14.0f}, 16.0f, Align::Center, Outlined(ui_color::kInk));
  canvas.Text(font, "\"Show me\" after a failed run is always free.", {r.Center().x, r.min.y + 38.0f}, 13.0f,
              Align::Center, Outlined(ui_color::kMuted));
  float y = r.min.y + 62.0f;
  const bool known = level_->pars.Known();
  auto row = [&](const std::string& label, Variant variant, Icon icon) {
    const bool clicked = wlw::ui::Button(ui, label, {{r.min.x + 8.0f, y}, {r.max.x - 8.0f, y + 46.0f}}, variant, icon);
    y += 56.0f;
    return clicked;
  };
  wlw::kit::Ads& ads = app_.GetAds();
  const bool ad_ready = ads.RewardedReady();
  if (!lab) {
    if (row("Place one piece  (1 hint)", hints >= 1 && known ? Variant::Primary : Variant::Ghost, Icon::Hint) &&
        hints >= 1 && known) {
      progress.SpendHints(1);
      ApplyHint();
      dialog_ = Dialog::None;
      return;
    }
    if (row("Show the solution  (2 hints)", hints >= 2 && known && !ghosts_ ? Variant::Secondary : Variant::Ghost,
            Icon::Help) &&
        hints >= 2 && known && !ghosts_) {
      progress.SpendHints(2);
      ShowSolution();
      dialog_ = Dialog::None;
      return;
    }
    const int today = app_.Today();
    const bool can_ad = ad_ready && progress.HintAdsLeft(today) > 0;
    if (row(can_ad ? "Watch an ad: +1 hint" : "No ad right now", can_ad ? Variant::Secondary : Variant::Ghost,
            Icon::Play) &&
        can_ad) {
      ThreadlockApp& app = app_;
      app.OfferRewarded([&app, today] {
        app.GetProgress().AddHints(1);
        app.GetProgress().HintAdWatched(today);
        app.Toast("+1 hint");
      });
    }
    if (!ghosts_ && known && ad_ready && row("Watch an ad: show the solution", Variant::Ghost, Icon::Play)) {
      ThreadlockApp& app = app_;
      LevelScreen* self = this;
      app.OfferRewarded([self] { self->ShowSolution(); });
      dialog_ = Dialog::None;
    }
  } else {
    if (row(ad_ready ? "Watch an ad: place one piece" : "No ad right now",
            ad_ready && known ? Variant::Primary : Variant::Ghost, Icon::Play) &&
        ad_ready && known) {
      LevelScreen* self = this;
      app_.OfferRewarded([self] { self->ApplyHint(); });
      dialog_ = Dialog::None;
      return;
    }
    if (!ghosts_ && known && ad_ready && row("Watch an ad: show the solution", Variant::Secondary, Icon::Help)) {
      LevelScreen* self = this;
      app_.OfferRewarded([self] { self->ShowSolution(); });
      dialog_ = Dialog::None;
    }
  }
}

void LevelScreen::DrawInfo(UIContext& ui) {
  if (info_piece_ < 0) {
    dialog_ = Dialog::None;
    return;
  }
  wlw::ui::Canvas& canvas = ui.canvas;
  const wlw::ui::Font& font = app_.GetFont();
  const Piece& piece = board_.PieceInfo(info_piece_).piece;
  std::string title = PieceName(piece.type);
  if (piece.type == PieceType::Overflow) title += " " + std::to_string(piece.n);
  const wlw::ui::DialogResult dialog = wlw::ui::Dialog(ui, title, {310.0f, 336.0f}, true);
  if (dialog.close) {
    dialog_ = Dialog::None;
    return;
  }
  const Rect& r = dialog.content;
  PieceLook look;
  look.setting = board_.Setting(info_piece_);
  look.spin = app_.Time() * 0.8f;
  look.locked = board_.Locked(info_piece_);
  DrawPiece(canvas, piece, {r.Center().x, r.min.y + 46.0f}, 1.5f, look, app_.GetPalette(), nullptr, nullptr, &font);
  const PieceText text = TextFor(piece);
  float y = r.min.y + 104.0f;
  for (const std::string& line : Wrap(font, text.text, 16.0f, r.Size().x - 24.0f)) {
    canvas.Text(font, line, {r.Center().x, y}, 16.0f, Align::Center, Outlined(ui_color::kInk));
    y += 21.0f;
  }
  y += 8.0f;
  const WhereText where = WhereFor(piece.type);
  canvas.Text(font, "Works on " + where.works, {r.Center().x, y}, 14.0f, Align::Center, Outlined(ui_color::kGood));
  y += 20.0f;
  if (!where.trap.empty()) {
    canvas.Text(font, "A trap on " + where.trap + ": it waits forever", {r.Center().x, y}, 14.0f, Align::Center,
                Outlined(ui_color::kBad));
    y += 20.0f;
  }
  canvas.Text(font, std::string("Tap it: ") + text.tap, {r.Center().x, y}, 14.0f, Align::Center,
              Outlined(ui_color::kMuted));
  if (board_.Locked(info_piece_)) {
    canvas.Text(font, "Placed by the level: it can't move.", {r.Center().x, y + 20.0f}, 14.0f, Align::Center,
                Outlined(ui_color::kBad));
  }
}

}  // namespace threadlock
