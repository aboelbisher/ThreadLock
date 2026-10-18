#include "progress.h"

#include <algorithm>
#include <type_traits>

#include "wlw/core/log.h"

namespace threadlock {
namespace {

using Json = wlw::kit::Json;

constexpr int kStartHints = 3;
constexpr int kHintAdsPerDay = 5;

template <typename T>
T Get(const Json& object, const char* key, T fallback) {
  if (!object.is_object() || !object.contains(key)) return fallback;
  const Json& value = object[key];
  if constexpr (std::is_same_v<T, bool>) {
    return value.is_boolean() ? value.get<bool>() : fallback;
  } else if constexpr (std::is_same_v<T, std::string>) {
    return value.is_string() ? value.get<std::string>() : fallback;
  } else {
    return value.is_number() ? value.get<T>() : fallback;
  }
}

std::set<std::string> ReadSet(const Json& object, const char* key) {
  std::set<std::string> out;
  if (object.is_object() && object.contains(key) && object[key].is_array()) {
    for (const Json& item : object[key]) {
      if (item.is_string()) out.insert(item.get<std::string>());
    }
  }
  return out;
}

}  // namespace

Progress::Progress(const std::string& folder) : progress_(folder, "progress", 1), device_(folder, "device", 1) {}

void Progress::Load() {
  progress_.Load();
  device_.Load();
  device_id_ = wlw::kit::DeviceId(device_);
  Read();
}

void Progress::Read() {
  const Json& p = progress_.Data();
  stars_.clear();
  if (p.contains("stars") && p["stars"].is_object()) {
    for (const auto& [level, bits] : p["stars"].items()) {
      if (bits.is_number_integer()) stars_[level] = bits.get<int>();
    }
  }
  shown_ = ReadSet(p, "shown");
  achievements_ = ReadSet(p, "achievements");
  two_answers_ = ReadSet(p, "two_answers");
  counters_ = p.contains("counters") ? wlw::kit::Counters::FromJson(p["counters"]) : wlw::kit::Counters{};
  daily_ = p.contains("daily") ? wlw::kit::DailyLog::FromJson(p["daily"]) : wlw::kit::DailyLog{};

  const Json& d = device_.Data();
  const Json settings_json = d.contains("settings") ? d["settings"] : Json::object();
  settings.sound = Get(settings_json, "sound", true);
  settings.haptics = Get(settings_json, "haptics", true);
  settings.music = Get(settings_json, "music", true);
  settings.reduced_motion = Get(settings_json, "reduced_motion", false);
  settings.palette = Get<std::string>(settings_json, "palette", "classic");
  hints_ = Get(d, "hints", kStartHints);
  hint_ads_day_ = Get(d, "hint_ads_day", 0);
  hint_ads_ = Get(d, "hint_ads", 0);
  ad_games_ = Get(d, "ad_games", 0);
  last_mend_ = Get(d, "last_mend", 0);
  const Json lab = d.contains("lab") ? d["lab"] : Json::object();
  lab_difficulty = Get<std::string>(lab, "difficulty", "medium");
  lab_seed = Get<int64_t>(lab, "seed", 1);
}

void Progress::Write() {
  Json& p = progress_.Data();
  Json stars = Json::object();
  for (const auto& [level, bits] : stars_) stars[level] = bits;
  p["stars"] = stars;
  p["shown"] = shown_;
  p["achievements"] = achievements_;
  p["two_answers"] = two_answers_;
  p["counters"] = counters_.ToJson();
  p["daily"] = daily_.ToJson();

  Json& d = device_.Data();
  d["settings"] = {{"sound", settings.sound},
                   {"haptics", settings.haptics},
                   {"music", settings.music},
                   {"reduced_motion", settings.reduced_motion},
                   {"palette", settings.palette}};
  d["hints"] = hints_;
  d["hint_ads_day"] = hint_ads_day_;
  d["hint_ads"] = hint_ads_;
  d["ad_games"] = ad_games_;
  d["last_mend"] = last_mend_;
  d["lab"] = {{"difficulty", lab_difficulty}, {"seed", lab_seed}};
}

void Progress::Save() {
  Write();
  if (!progress_.SaveIfDirty()) WLW_LOG_WARN("Couldn't save %s", progress_.Path().c_str());
  if (!device_.SaveIfDirty()) WLW_LOG_WARN("Couldn't save %s", device_.Path().c_str());
}

void Progress::Wipe() {
  progress_.Wipe();
  device_.Wipe();
  device_id_ = wlw::kit::DeviceId(device_);
  Read();
  progress_.MarkDirty();
  device_.MarkDirty();
}

// ---- Stars ----

Progress::Earned Progress::RecordSolve(const Level& level, int pieces, int ticks, bool shown) {
  Earned earned;
  earned.bits = kSolveStar;
  if (!shown && level.pars.Known()) {
    if (pieces <= level.pars.pieces) earned.bits |= kLeanStar;
    if (ticks <= level.pars.ticks) earned.bits |= kSwiftStar;
    if ((earned.bits & kLeanStar) && (earned.bits & kSwiftStar)) earned.bits |= kGoldFrame;
  }
  int& bits = stars_[level.id];
  earned.first_solve = !(bits & kSolveStar);
  earned.fresh = earned.bits & ~bits;
  // Lean from this run and Swift from another (or the other way round): two different answers.
  const bool lean_only = (earned.bits & kLeanStar) && !(earned.bits & kSwiftStar) && (bits & kSwiftStar);
  const bool swift_only = (earned.bits & kSwiftStar) && !(earned.bits & kLeanStar) && (bits & kLeanStar);
  if (lean_only || swift_only) two_answers_.insert(level.id);
  if (earned.fresh) {
    bits |= earned.bits;
    progress_.MarkDirty();
  }
  return earned;
}

int Progress::Bits(std::string_view level) const {
  const auto it = stars_.find(std::string(level));
  return it == stars_.end() ? 0 : it->second;
}

int Progress::StarCount(std::string_view level) const {
  const int bits = Bits(level);
  return ((bits & kSolveStar) ? 1 : 0) + ((bits & kLeanStar) ? 1 : 0) + ((bits & kSwiftStar) ? 1 : 0);
}

void Progress::MarkSolutionShown(std::string_view level) {
  if (shown_.insert(std::string(level)).second) progress_.MarkDirty();
}

int Progress::WorldStars(const Catalog& catalog, int world) const {
  int stars = 0;
  for (const Level* level : catalog.MainPath(world)) stars += StarCount(level->id);
  return stars;
}

int Progress::StarsBefore(const Catalog& catalog, int world) const {
  int stars = 0;
  for (int w = 1; w < world; ++w) stars += WorldStars(catalog, w);
  return stars;
}

int Progress::TotalStars() const {
  int stars = 0;
  for (const auto& [level, bits] : stars_) stars += StarCount(level);
  return stars;
}

// ---- What's open ----

bool Progress::WorldOpen(const Catalog& catalog, int world) const {
  if (world <= 1) return true;
  return catalog.GetWorld(world) != nullptr && StarsBefore(catalog, world) >= Catalog::Gate(world);
}

bool Progress::LevelOpen(const Catalog& catalog, const Level& level) const {
  if (!WorldOpen(catalog, level.world)) return false;
  if (level.type == LevelType::Challenge) {
    const std::vector<const Level*> path = catalog.MainPath(level.world);
    return !path.empty() && Solved(path.back()->id);
  }
  const std::vector<const Level*> path = catalog.MainPath(level.world);
  for (size_t i = 0; i < path.size(); ++i) {
    if (path[i] == &level) return i == 0 || Solved(path[i - 1]->id);
  }
  return false;
}

const Level* Progress::Current(const Catalog& catalog) const {
  const Level* last_open = nullptr;
  for (const World& world : catalog.Worlds()) {
    if (!WorldOpen(catalog, world.number)) break;
    for (const Level* level : catalog.MainPath(world.number)) {
      if (!LevelOpen(catalog, *level)) break;
      if (!Solved(level->id)) return level;
      last_open = level;
    }
  }
  return last_open;
}

bool Progress::LabOpen(const Catalog& catalog) const {
  const std::vector<const Level*> path = catalog.MainPath(2);
  return !path.empty() && Solved(path.back()->id);
}

// ---- Counters and achievements ----

void Progress::Count(const std::string& name, int64_t amount) {
  counters_.Add(device_id_, name, amount);
  progress_.MarkDirty();
}

bool Progress::Achieve(const std::string& key) {
  if (!achievements_.insert(key).second) return false;
  progress_.MarkDirty();
  return true;
}

int Progress::AchievementProgress(const Achievement& achievement) const {
  const std::string_view key = achievement.key;
  if (key == "threadlocked") return static_cast<int>(std::min<int64_t>(Counter("jams"), 1));
  if (key == "close_shave") return static_cast<int>(std::min<int64_t>(Counter("near_misses"), achievement.goal));
  if (key == "lean_machine") {
    int lean = 0;
    for (const auto& [level, bits] : stars_) lean += (bits & kLeanStar) ? 1 : 0;
    return std::min(lean, achievement.goal);
  }
  if (key == "two_answers") return std::min(static_cast<int>(two_answers_.size()), achievement.goal);
  if (key == "daily_week") return std::min(daily_.BestStreak(), achievement.goal);
  if (key == "first_world") return Solved("1-15") ? 1 : 0;
  return 0;
}

std::vector<std::string> Progress::UpdateAchievements(const Catalog& catalog) {
  (void)catalog;
  std::vector<std::string> fresh;
  for (const Achievement& achievement : kAchievements) {
    if (AchievementProgress(achievement) >= achievement.goal && Achieve(achievement.key))
      fresh.push_back(achievement.key);
  }
  return fresh;
}

// ---- Daily ----

bool Progress::MarkDaily(int date) {
  if (!daily_.Mark(date)) return false;
  progress_.MarkDirty();
  return true;
}

bool Progress::CanMendStreak(int today) const {
  if (last_mend_ == 0) return true;
  const int64_t days =
      wlw::kit::Date::FromNumber(today).DayNumber() - wlw::kit::Date::FromNumber(last_mend_).DayNumber();
  return days >= 7;
}

void Progress::MendStreak(int date, int today) {
  MarkDaily(date);
  last_mend_ = today;
  device_.MarkDirty();
}

// ---- Hints ----

void Progress::AddHints(int amount) {
  hints_ = std::max(0, hints_ + amount);
  device_.MarkDirty();
}

bool Progress::SpendHints(int amount) {
  if (hints_ < amount) return false;
  hints_ -= amount;
  Count("hints_used", amount);
  device_.MarkDirty();
  return true;
}

int Progress::HintAdsLeft(int today) const { return kHintAdsPerDay - (hint_ads_day_ == today ? hint_ads_ : 0); }

void Progress::HintAdWatched(int today) {
  if (hint_ads_day_ != today) {
    hint_ads_day_ = today;
    hint_ads_ = 0;
  }
  ++hint_ads_;
  device_.MarkDirty();
}

void Progress::SetAdGames(int games) {
  if (games == ad_games_) return;
  ad_games_ = games;
  device_.MarkDirty();
}

// ---- Collections ----

bool Progress::PaletteOwned(const Catalog& catalog, std::string_view key) const {
  auto challenges_done = [&](int world) {
    const std::vector<const Level*> challenges = catalog.Challenges(world);
    return !challenges.empty() &&
           std::all_of(challenges.begin(), challenges.end(), [&](const Level* level) { return Solved(level->id); });
  };
  if (key == "classic" || key == "contrast") return true;
  if (key == "neon") return challenges_done(1);
  if (key == "sunset") return challenges_done(2);
  if (key == "deep_sea") return daily_.BestStreak() >= 7;
  if (key == "prism") return daily_.BestStreak() >= 30;
  return false;
}

const char* Progress::PaletteUnlockText(std::string_view key) {
  if (key == "neon") return "Solve World 1's challenges";
  if (key == "sunset") return "Solve World 2's challenges";
  if (key == "deep_sea") return "A 7-day daily streak";
  if (key == "prism") return "A 30-day daily streak";
  return "";
}

}  // namespace threadlock
