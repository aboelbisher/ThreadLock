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

// ---- The sanctum's still picture (the art draws these; the tests pin them) ----

// A glass vessel: a flask (a spout) or a phial (a cup), its round body and the way its neck points.
struct Vessel {
  Vec2 centre;
  float r = 0.0f;
  Vec2 up{0.0f, -1.0f};  // toward its neck
};
// A spout's element flask: r 23 (down to 14 so its body and neck stay on the board), its body behind the node
// (centre = node - u (r - 7), u = its first groove's way out), the neck up.
Vessel FlaskAt(const Level& level, int spout);
// A cup's phial: r 24 for an order of 5 or more, else 15, hanging past the node the way its last groove runs
// (centre = node + u (r + 9)), its neck back toward the groove.
Vessel PhialAt(const Level& level, int cup);
// A phial's pill (glyph + count): under it (over it when it hangs neck down, or when under would leave the board).
Vec2 PillAt(const Level& level, int cup);
inline constexpr float kFlaskNeck = 17.0f, kPhialNeck = 22.0f, kPillHeight = 18.0f;
// The glass round a junction: a socket's globe 26 (the piece shows inside it), a fixed junction's 15, a twin
// half's seal 15.
float GlobeRadius(const Level& level, int node);
// The angles (degrees, 0 = +x, clockwise) where the grooves meet the ring of radius `ring` round the sigil's
// centre: the runes leave room for them.
std::vector<float> Ports(const Level& level, const Sigil& sigil, float ring);
// The runes cut round a ring, just inside it: 40 slots, 2 or 3 short strokes each, none near a port; seeded by
// the level's id (the same on every device).
struct Stroke2 {
  Vec2 a, b;
};
std::vector<Stroke2> Runes(const Level& level, const Sigil& sigil, float ring);
// The figure: the junctions on the sigil's points, by angle round its centre.
std::vector<Vec2> Figure(const Level& level, const Sigil& sigil);
// What the table's inlay draws of the figure (closed polygons): a regular triangle and its half turn (a hexagram),
// six points a sixth apart as their two triangles, any other figure as itself.
std::vector<std::vector<Vec2>> FigureLines(const Level& level, const Sigil& sigil);
// The candles at the board's corners (each screen of a tall board has its four), nudged off the vessels and pills.
std::vector<Vec2> Candles(const Level& level);

// ---- The moments ----

// The payoff's times (seconds after a phial fills): its grooves start to ignite at kIgniteStart; once the run is
// solved the whole sigil is lit by kSolvedLength (one sigil) or kGrandLength (the Great Work's two). The sounds
// (audio_synth) are timed by them too.
inline constexpr float kIgniteStart = 0.5f, kSolvedLength = 3.3f, kGrandLength = 4.0f;
inline constexpr float kIgniteSpeed = 260.0f;  // board units a second, back up the grooves
// A phial's grooves lighting back up toward the flasks that fed it: each groove upstream of the cup (a crossing's
// lanes kept apart), from its downstream end, starting `delay` seconds after the phial is full and taking `length`.
struct Ignite {
  int pipe = -1;
  float delay = 0.0f, length = 0.0f;
};
std::vector<Ignite> IgniteFrom(const Level& level, int cup);
// The colour that would come to meet a drop waiting at a mixer (on its in `lane`) or a twin half (lane ignored):
// what the flasks upstream of the other way in pour, if they all pour one colour; Color::None if not.
Color PartnerColor(const Level& level, int node, int lane);
// The way that partner would come: the groove points from its flask to the node (a twin half's partner arrives at
// the other half, then along the bar). Empty if no flask feeds it.
std::vector<Vec2> PartnerRoute(const Level& level, int node, int lane);
// A circle of waiting (JamReport::cycle) along the grooves: the shortest way from each node to the next, closed
// (it ends where it starts). Empty if the nodes aren't joined.
std::vector<Vec2> JamLoop(const Level& level, const std::vector<int>& cycle);

// The result card's words for a failed run on a sigil (the level screen's FailureText for World 4): sludge in a
// globe (the two colours that met) or in a phial, the phial that got the wrong colour, too much or too little, and
// the jam (the lonely mixer or seal and the partner it waits for, or the mixers holding each other) with every
// phial's count.
std::string FailureText(const Level& level, const Board& board, const Outcome& outcome);

}  // namespace threadlock::sigil
