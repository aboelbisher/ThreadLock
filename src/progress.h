#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <string_view>

#include "catalog.h"
#include "wlw/kit/daily.h"
#include "wlw/kit/progress.h"
#include "wlw/kit/save_store.h"

// What Threadlock remembers between launches, in two files (wlw::kit::SaveStore), like Bluzzle:
//
//   progress.json  the player's progress, built only from mergeable shapes (a union of star bits per
//                  level, sets, per-device counters, the daily log), so a later cloud sync can
//                  combine two devices without losing anything.
//   device.json    this device: settings, hints (a currency, not synced), ad pacing, the Lab.
//
// The rules of progress live here too: stars (Solve / Lean / Swift, DESIGN.md §6), which levels and
// worlds are open, achievements.
namespace threadlock {

enum StarBits : int {
  kSolveStar = 1,
  kLeanStar = 2,
  kSwiftStar = 4,
  kGoldFrame = 8,  // Lean and Swift in the same run
};

struct Settings {
  bool sound = true;
  bool haptics = true;
  bool music = true;  // the ambience (World 1)
  bool reduced_motion = false;
  std::string palette = "classic";
};

struct Achievement {
  const char* key;
  const char* name;
  const char* text;
  int goal;  // for a progress bar (1 = a one-off)
};
inline constexpr Achievement kAchievements[] = {
    {"threadlocked", "Threadlocked", "Your first deadlock.", 1},
    {"close_shave", "Close Shave", "100 near misses.", 100},
    {"lean_machine", "Lean Machine", "Lean on 20 levels.", 20},
    {"two_answers", "Two Answers", "Lean and Swift with different machines on 10 levels.", 10},
    {"daily_week", "Regular", "A 7-day daily streak.", 7},
    {"first_world", "First Light", "Finish World 1.", 1},
};

class Progress {
 public:
  explicit Progress(const std::string& folder);  // the game passes platform::GetSaveDirectory()
  void Load();
  void Save();  // writes what changed
  void Wipe();

  // ---- Stars ----
  struct Earned {
    int bits = 0;   // what this run earned
    int fresh = 0;  // what's new for the level
    bool first_solve = false;
  };
  // A solved run of `level`: `pieces` placed, finished in `ticks`. With `shown` (the player is
  // running the solution they were shown), only the Solve star counts.
  Earned RecordSolve(const Level& level, int pieces, int ticks, bool shown);
  int Bits(std::string_view level) const;
  int StarCount(std::string_view level) const;  // 0..3
  bool Solved(std::string_view level) const { return Bits(level) & kSolveStar; }
  void MarkSolutionShown(std::string_view level);
  bool SolutionShown(std::string_view level) const { return shown_.count(std::string(level)) != 0; }
  int WorldStars(const Catalog& catalog, int world) const;   // its main path's stars
  int StarsBefore(const Catalog& catalog, int world) const;  // every main-path star before `world`
  int TotalStars() const;

  // ---- What's open ----
  bool WorldOpen(const Catalog& catalog, int world) const;
  bool LevelOpen(const Catalog& catalog, const Level& level) const;
  // The level to play next: the first open, unsolved one on the main path (else the last open one).
  const Level* Current(const Catalog& catalog) const;
  bool LabOpen(const Catalog& catalog) const;     // after World 2
  bool MapOpen() const { return Solved("1-3"); }  // first launch goes straight to the levels

  // ---- Counters and achievements ----
  void Count(const std::string& name, int64_t amount = 1);
  int64_t Counter(const std::string& name) const { return counters_.Get(name); }
  bool Achieve(const std::string& key);  // true the first time
  bool Achieved(const std::string& key) const { return achievements_.count(key) != 0; }
  int AchievementProgress(const Achievement& achievement) const;
  // Checks every achievement against the current numbers; returns the keys newly earned.
  std::vector<std::string> UpdateAchievements(const Catalog& catalog);

  // ---- Daily puzzle ----
  wlw::kit::DailyLog& Daily() { return daily_; }
  const wlw::kit::DailyLog& Daily() const { return daily_; }
  bool MarkDaily(int date);
  bool CanMendStreak(int today) const;  // a missed day may mend the streak once a week
  void MendStreak(int date, int today);

  // ---- Hints (this device) ----
  int Hints() const { return hints_; }
  void AddHints(int amount);
  bool SpendHints(int amount);
  int HintAdsLeft(int today) const;  // rewarded hints: at most 5 a day
  void HintAdWatched(int today);

  // ---- This device ----
  Settings settings;
  void SettingsChanged() { device_.MarkDirty(); }
  int AdGames() const { return ad_games_; }  // solves counted for ad pacing, kept across sessions
  void SetAdGames(int games);
  std::string lab_difficulty = "medium";
  int64_t lab_seed = 1;
  void LabChanged() { device_.MarkDirty(); }

  // Collections (DESIGN.md §7): which liquid palettes this player has.
  bool PaletteOwned(const Catalog& catalog, std::string_view key) const;
  static const char* PaletteUnlockText(std::string_view key);

  const std::string& DeviceId() const { return device_id_; }

 private:
  void Read();
  void Write();

  wlw::kit::SaveStore progress_;
  wlw::kit::SaveStore device_;
  std::string device_id_;
  std::map<std::string, int> stars_;  // level -> StarBits (merged with OR)
  std::set<std::string> shown_;
  std::set<std::string> achievements_;
  std::set<std::string> two_answers_;  // levels where Lean and Swift came from different runs
  wlw::kit::Counters counters_;
  wlw::kit::DailyLog daily_;
  int hints_ = 3;
  int hint_ads_day_ = 0;
  int hint_ads_ = 0;
  int ad_games_ = 0;
  int last_mend_ = 0;
};

}  // namespace threadlock
