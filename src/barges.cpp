#include "barges.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace threadlock::barges {
namespace {

constexpr float kSlide = 8.0f;       // a barge slides this far out of its lock as it pours (board units)
constexpr float kGlideTicks = 1.5f;  // a landed barge glides into its berth over this many ticks
constexpr float kLeafOpen = 0.25f, kLeafHeld = 0.75f;  // ticks: the leaves open by the first, shut by 1.0
constexpr float kGateSwing = 0.6f;                     // ticks: a gate's leaves swing from one state to the other

Vec2 Unit(Vec2 v) {
  const float length = std::sqrt(v.x * v.x + v.y * v.y);
  return length > 1e-5f ? Vec2{v.x / length, v.y / length} : Vec2{0.0f, 1.0f};
}

float CellDistance(const Pipe& pipe, int cell) {
  return (static_cast<float>(cell) + 0.5f) * pipe.length / static_cast<float>(pipe.cells);
}

// A drop's place: on its pipe (along the canal) or in its node (the rules' point).
Vec2 PlaceOf(const Level& level, const std::vector<canal::Way>& ways, const Drop& drop, const Loc& loc) {
  if (loc.kind == Loc::OnPipe) {
    const size_t pipe = static_cast<size_t>(loc.index);
    return ways[pipe].PointAt(CellDistance(level.pipes[pipe], loc.cell));
  }
  return DropPoint(level, drop, loc);
}

// A drop poured this tick: it starts in its spout (a lock) and slides out onto the canal.
bool JustPoured(const Level& level, const Drop& drop) {
  return drop.prev.kind == Loc::None || (drop.prev.kind == Loc::InNode && drop.prev.index >= 0 &&
                                         level.nodes[static_cast<size_t>(drop.prev.index)].kind == NodeKind::Spout);
}

// Which way a drop at `loc` heads: along its pipe, or out of the node it's in (its lane out, else its lane in).
Vec2 HeadingOf(const Level& level, const std::vector<canal::Way>& ways, const Drop& drop, const Loc& loc) {
  if (loc.kind == Loc::OnPipe) {
    const size_t pipe = static_cast<size_t>(loc.index);
    return Unit(ways[pipe].DirectionAt(CellDistance(level.pipes[pipe], loc.cell)));
  }
  if (loc.kind == Loc::InNode && loc.index >= 0) {
    const Node& node = level.nodes[static_cast<size_t>(loc.index)];
    if (drop.lane_out >= 0 && static_cast<size_t>(drop.lane_out) < node.outs.size()) {
      return Unit(ways[static_cast<size_t>(node.outs[static_cast<size_t>(drop.lane_out)])].DirectionAt(0.0f));
    }
    if (drop.lane_in >= 0 && static_cast<size_t>(drop.lane_in) < node.ins.size()) {
      const size_t pipe = static_cast<size_t>(node.ins[static_cast<size_t>(drop.lane_in)]);
      return Unit(ways[pipe].DirectionAt(level.pipes[pipe].length));
    }
  }
  return {0.0f, 1.0f};
}

}  // namespace

Pose PoseOf(const Level& level, const std::vector<canal::Way>& ways, const Drop& drop, float alpha) {
  alpha = std::clamp(alpha, 0.0f, 1.0f);
  const Loc& now = drop.loc;
  Pose pose;
  if (JustPoured(level, drop) && now.kind == Loc::OnPipe) {
    // Just poured: it slides out of its lock, fading in.
    const size_t pipe = static_cast<size_t>(now.index);
    const float d = CellDistance(level.pipes[pipe], now.cell);
    pose.at = ways[pipe].PointAt(d - kSlide * (1.0f - alpha));
    pose.heading = Unit(ways[pipe].DirectionAt(d));
    pose.fade = alpha;
    pose.moving = true;
    return pose;
  }
  if (drop.prev.kind == Loc::OnPipe && now.kind == Loc::OnPipe && drop.prev.index == now.index) {
    // Along its pipe, round the bends (the canal's own way, not a straight line across them).
    const size_t pipe = static_cast<size_t>(now.index);
    const Pipe& rules = level.pipes[pipe];
    const float d0 = CellDistance(rules, drop.prev.cell), d1 = CellDistance(rules, now.cell);
    const float d = d0 + (d1 - d0) * alpha;
    pose.at = ways[pipe].PointAt(d);
    pose.heading = Unit(ways[pipe].DirectionAt(d));
    pose.moving = !(drop.prev == drop.loc);
    return pose;
  }
  const Vec2 here = PlaceOf(level, ways, drop, now);
  const Vec2 before = drop.prev.kind == Loc::None ? here : PlaceOf(level, ways, drop, drop.prev);
  pose.at = {before.x + (here.x - before.x) * alpha, before.y + (here.y - before.y) * alpha};
  const Vec2 move{here.x - before.x, here.y - before.y};
  pose.moving = !(drop.prev == drop.loc);
  pose.heading = std::sqrt(move.x * move.x + move.y * move.y) > 1e-3f ? Unit(move) : HeadingOf(level, ways, drop, now);
  return pose;
}

float LockOpen(float since) {
  const float up = std::clamp(since / kLeafOpen, 0.0f, 1.0f);
  const float down = std::clamp((since - kLeafHeld) / (1.0f - kLeafHeld), 0.0f, 1.0f);
  return up * (1.0f - down);
}

float Glide(float since) {
  const float t = std::clamp(since / kGlideTicks, 0.0f, 1.0f);
  return 1.0f - (1.0f - t) * (1.0f - t);
}

float GateOpenness(bool open_now, float since_flip) {
  const float t = std::clamp(since_flip / kGateSwing, 0.0f, 1.0f);
  const float eased = 1.0f - (1.0f - t) * (1.0f - t);
  return open_now ? eased : 1.0f - eased;
}

float LampsAt(float age) { return std::clamp(age / kWarmLength, 0.0f, 1.0f); }

float WarmAt(float age) { return std::sin(std::numbers::pi_v<float> * std::clamp(age / kWarmLength, 0.0f, 1.0f)); }

float DistrictAt(float age) {
  const float t = std::clamp(age / kDistrictLength, 0.0f, 1.0f);
  return 1.0f - (1.0f - t) * (1.0f - t);
}

float CityAt(float age) {
  const float t = std::clamp(age / kCityLength, 0.0f, 1.0f);
  return 1.0f - (1.0f - t) * (1.0f - t);
}

}  // namespace threadlock::barges
