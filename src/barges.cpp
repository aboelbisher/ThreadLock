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

// A drop's place: on its pipe (along the canal), or in its node (its mooring there, else the rules' point).
Vec2 PlaceOf(const Level& level, const std::vector<canal::Way>& ways, const Drop& drop, const Loc& loc,
             const Moorings* moorings) {
  if (loc.kind == Loc::OnPipe) {
    const size_t pipe = static_cast<size_t>(loc.index);
    return ways[pipe].PointAt(CellDistance(level.pipes[pipe], loc.cell));
  }
  if (moorings && loc.kind == Loc::InNode && moorings->Has(drop.id, loc.index)) return moorings->At(drop.id);
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
    const Vec2 along = Unit(ways[pipe].DirectionAt(CellDistance(level.pipes[pipe], loc.cell)));
    return drop.dir < 0 ? Vec2{-along.x, -along.y} : along;
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

Vec2 BerthPoint(const Node& basin, int k) {
  const int berths = std::max(1, basin.berths);
  if (berths == 1) return basin.at;
  const float a = -1.5708f + 6.2832f * (static_cast<float>(k) + 0.5f) / static_cast<float>(berths);
  const float r = canal::BasinRadius(berths) * 0.55f;
  return {basin.at.x + std::cos(a) * r, basin.at.y + std::sin(a) * r};
}

const Moorings::Spot* Moorings::Find(int drop_id) const {
  for (const auto& [id, spot] : spots_) {
    if (id == drop_id) return &spot;
  }
  return nullptr;
}

bool Moorings::Has(int drop_id, int node) const {
  const Spot* spot = Find(drop_id);
  return spot && spot->node == node;
}

Vec2 Moorings::At(int drop_id) const {
  const Spot* spot = Find(drop_id);
  return spot ? spot->at : Vec2{};
}

void Moorings::Clear() {
  spots_.clear();
  tick_ = -1;
}

void Moorings::Update(const Level& level, const std::vector<canal::Way>& ways, const Board& board) {
  if (!board.Started() || board.Ticks() < tick_) Clear();
  if (!board.Started() || board.Ticks() == tick_) return;
  tick_ = board.Ticks();
  const std::vector<Drop>& drops = board.Drops();
  // Forget the barges that landed; a barge that left keeps its spot for this tick (it slides out of it).
  std::erase_if(spots_, [&](const std::pair<int, Spot>& entry) {
    return std::none_of(drops.begin(), drops.end(), [&](const Drop& d) { return d.alive && d.id == entry.first; });
  });
  for (const Drop& drop : drops) {
    if (!drop.alive || drop.loc.kind != Loc::InNode) continue;
    const int node = drop.loc.index;
    if (Has(drop.id, node)) continue;
    const Node& n = level.nodes[static_cast<size_t>(node)];
    Spot spot{node, -1, n.at};
    if (n.kind == NodeKind::Basin) {
      // The first berth no barge in this basin holds.
      std::vector<bool> taken(static_cast<size_t>(std::max(1, n.berths)), false);
      for (const Drop& other : drops) {
        if (!other.alive || other.id == drop.id || other.loc.kind != Loc::InNode || other.loc.index != node) continue;
        const Spot* held = Find(other.id);
        if (held && held->node == node && held->berth >= 0 && static_cast<size_t>(held->berth) < taken.size())
          taken[static_cast<size_t>(held->berth)] = true;
      }
      int k = 0;
      while (static_cast<size_t>(k) < taken.size() && taken[static_cast<size_t>(k)]) ++k;
      if (static_cast<size_t>(k) == taken.size()) k = 0;
      spot.berth = k;
      spot.at = BerthPoint(n, k);
    } else if (n.kind == NodeKind::Pass && board.PieceAt(node) >= 0 &&
               board.PieceInfo(board.PieceAt(node)).piece.type == PieceType::Passing && drop.via >= 0) {
      // A passing place: abreast, each barge on its own side of the canal (by the way it came in).
      const size_t via = static_cast<size_t>(drop.via);
      const Vec2 along = Unit(ways[via].DirectionAt(level.pipes[via].to == node ? level.pipes[via].length : 0.0f));
      const float side = !n.ins.empty() && drop.via == n.ins[0] ? -7.0f : 7.0f;
      spot.at = {n.at.x - along.y * side, n.at.y + along.x * side};
    }
    bool replaced = false;
    for (auto& [id, held] : spots_) {
      if (id == drop.id) {
        held = spot;
        replaced = true;
      }
    }
    if (!replaced) spots_.push_back({drop.id, spot});
  }
}

Pose PoseOf(const Level& level, const std::vector<canal::Way>& ways, const Drop& drop, float alpha,
            const Moorings* moorings) {
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
    if (drop.dir < 0) pose.heading = {-pose.heading.x, -pose.heading.y};  // two-way canals: heading back up it
    pose.moving = !(drop.prev == drop.loc);
    return pose;
  }
  const Vec2 here = PlaceOf(level, ways, drop, now, moorings);
  const Vec2 before = drop.prev.kind == Loc::None ? here : PlaceOf(level, ways, drop, drop.prev, moorings);
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
