#pragma once

#include <memory>
#include <string_view>
#include <vector>

#include "level_io.h"

// Every handmade level: assets/levels/world<N>.json + the solver's pars (pars.json), loaded once
// at startup (platform::ReadFile, so it works from the APK). Levels live here for the whole run,
// so boards and screens can keep pointers to them.
namespace threadlock {

class Catalog {
 public:
  bool Load();  // false (and logged) if a file is missing or broken

  const std::vector<World>& Worlds() const { return worlds_; }
  int WorldCount() const { return static_cast<int>(worlds_.size()); }
  const World* GetWorld(int number) const;
  const Level* Find(std::string_view id) const;

  // A world's main path (its numbered levels, in order) and its optional challenges.
  std::vector<const Level*> MainPath(int world) const;
  std::vector<const Level*> Challenges(int world) const;
  bool IsFinale(const Level& level) const;  // the last level of its world's main path
  // The level after this one on the main path, into the next world after a finale (null at the end).
  const Level* NextOnPath(const Level& level) const;

  // Stars needed to open a world: about 45% of the stars before it (DESIGN.md §7).
  static int Gate(int world) { return world <= 1 ? 0 : 20 * (world - 1); }

 private:
  std::vector<World> worlds_;
};

}  // namespace threadlock
