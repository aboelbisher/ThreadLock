#pragma once

#include <map>
#include <string>

// A level's stars (DESIGN.md §6) as bits, kept per level id. Plain C++: the progress (progress.h) and
// the city map (city_map.h) both use it.
namespace threadlock {

enum StarBits : int {
  kSolveStar = 1,
  kLeanStar = 2,
  kSwiftStar = 4,
  kGoldFrame = 8,  // Lean and Swift in the same run
};

using StarMap = std::map<std::string, int>;  // level id -> StarBits

inline int StarsIn(int bits) {
  return ((bits & kSolveStar) ? 1 : 0) + ((bits & kLeanStar) ? 1 : 0) + ((bits & kSwiftStar) ? 1 : 0);
}

}  // namespace threadlock
