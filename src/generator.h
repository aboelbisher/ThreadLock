#pragma once

#include <cstdint>
#include <string>

#include "threadlock_rules.h"

// Generated levels: the daily puzzle (the same for everyone on a date, easy on Monday, hard on
// Sunday) and the Lab (endless, at a chosen difficulty). DESIGN.md §6, §8.
//
// Each comes from a template (a race of crossing lanes, a fair-shares tree of forks, a mixer, a
// shared pipe) with random lengths, beats, counts, sockets and tray, and is kept only if the solver
// says it's a good puzzle for its difficulty: solvable, the empty board fails, enough pieces
// needed, and few enough of all placements solve it. Deterministic: a seed is always the same level.
// Plain C++ like the rules (tested on the PC).
namespace threadlock {

// Difficulty 0 (gentle) .. 6 (hard).
// `kind` picks a template (0 race, 1 shares, 2 mixer, 3 shared pipe); -1 = by difficulty.
Level Generate(uint64_t seed, int difficulty, int kind = -1);

int DailyWeekday(int yyyymmdd);     // 0 = Monday
int DailyDifficulty(int yyyymmdd);  // Monday 0 .. Sunday 6
const char* DailyDifficultyName(int difficulty);
Level GenerateDaily(int yyyymmdd);
// The Lab: "easy" / "medium" / "hard".
Level GenerateLab(int64_t seed, const std::string& difficulty);

}  // namespace threadlock
