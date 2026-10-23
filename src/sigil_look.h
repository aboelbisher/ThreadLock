#pragma once

#include <string>
#include <vector>

#include "threadlock_rules.h"

// World 4's sanctum, in board units (plain C++: the level tool checks levels against it, the art draws it).
// The machine is a sigil inlaid in the table: rings round one centre (Level::LevelLook::sigil), the junctions
// (glass globes) on its points, the grooves straight lines between them or arcs along its rings ("arc" pipes),
// the element flasks (spouts) and phials (cups) outside it.
namespace threadlock::sigil {

// A level drawn as a sigil: it has rings (the marker, as World 2's shelves are the shop's).
bool IsSigil(const Level& level);

// A pipe's groove: from its node's centre through its bends (an arc's points along its ring) to the next
// node's centre.
std::vector<Vec2> GroovePoints(const Level& level, const Pipe& pipe);

// What's wrong with a sigil (empty = good; LookProblems calls it): its centre or a ring off the board; a
// junction neither on a ring point nor on the figure's lines (straight between two junctions on ring points,
// as 4-7's crossings sit on its triangle's sides); a spout or cup inside the
// outer ring; a groove through a node it doesn't join, along another groove (or itself) or across one outside
// a crossing; a twin pair's bar over another node.
std::vector<std::string> Problems(const Level& level);

}  // namespace threadlock::sigil
