#include "catalog.h"

#include <string>

#include "wlw/core/log.h"
#include "wlw/platform/platform.h"

namespace threadlock {
namespace {

std::string ReadAsset(const std::string& path) {
  const auto bytes = wlw::platform::ReadFile(path);
  return bytes ? std::string(bytes->begin(), bytes->end()) : std::string();
}

}  // namespace

bool Catalog::Load() {
  worlds_.clear();
  bool ok = true;
  for (int n = 1; n <= 8; ++n) {
    const std::string text = ReadAsset("levels/world" + std::to_string(n) + ".json");
    if (text.empty()) break;
    World world;
    std::vector<std::string> errors;
    if (!ParseWorld(text, world, errors)) ok = false;
    for (const std::string& error : errors) WLW_LOG_ERROR("levels/world%d.json: %s", n, error.c_str());
    worlds_.push_back(std::move(world));
  }
  const std::string pars = ReadAsset("levels/pars.json");
  std::vector<std::string> errors;
  if (pars.empty() || !ApplyPars(pars, worlds_, errors)) ok = false;
  for (const std::string& error : errors) WLW_LOG_ERROR("levels/pars.json: %s", error.c_str());
  int levels = 0;
  for (const World& world : worlds_) levels += static_cast<int>(world.levels.size());
  WLW_LOG_INFO("Catalog: %d worlds, %d levels%s", WorldCount(), levels, ok ? "" : " (with problems)");
  return ok && !worlds_.empty();
}

const World* Catalog::GetWorld(int number) const {
  for (const World& world : worlds_) {
    if (world.number == number) return &world;
  }
  return nullptr;
}

const Level* Catalog::Find(std::string_view id) const {
  for (const World& world : worlds_) {
    for (const Level& level : world.levels) {
      if (level.id == id) return &level;
    }
  }
  return nullptr;
}

std::vector<const Level*> Catalog::MainPath(int number) const {
  std::vector<const Level*> path;
  if (const World* world = GetWorld(number)) {
    for (const Level& level : world->levels) {
      if (level.type != LevelType::Challenge) path.push_back(&level);
    }
  }
  return path;
}

std::vector<const Level*> Catalog::Challenges(int number) const {
  std::vector<const Level*> challenges;
  if (const World* world = GetWorld(number)) {
    for (const Level& level : world->levels) {
      if (level.type == LevelType::Challenge) challenges.push_back(&level);
    }
  }
  return challenges;
}

bool Catalog::IsFinale(const Level& level) const {
  const std::vector<const Level*> path = MainPath(level.world);
  return !path.empty() && path.back() == &level;
}

const Level* Catalog::NextOnPath(const Level& level) const {
  if (level.type == LevelType::Challenge) return nullptr;
  const std::vector<const Level*> path = MainPath(level.world);
  for (size_t i = 0; i + 1 < path.size(); ++i) {
    if (path[i] == &level) return path[i + 1];
  }
  const std::vector<const Level*> next = MainPath(level.world + 1);
  return next.empty() ? nullptr : next.front();
}

}  // namespace threadlock
