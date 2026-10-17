#include "solver.h"

#include <algorithm>
#include <tuple>

namespace threadlock {
namespace {

size_t Index(int i) { return static_cast<size_t>(i); }

bool IsRouter(PieceType type) {
  return type == PieceType::Separator || type == PieceType::Flipper || type == PieceType::Overflow;
}

struct Search {
  const Level& level;
  const std::function<bool(const Placement&)>& visit;
  std::vector<int> movable;               // tray pieces the search places
  std::vector<int> same_as;               // per movable piece: the previous identical one (-1 = none)
  std::vector<std::vector<int>> choices;  // per movable piece: the sockets where it acts
  std::vector<int> taps;                  // forks with a tappable flap
  std::vector<int> taken;                 // per node: the PieceType on it in this placement, -1 = free
  Placement placement;
  bool stopped = false;

  void Pieces(size_t k) {
    if (stopped) return;
    if (k == movable.size()) {
      Flaps(0);
      return;
    }
    const int piece = movable[k];
    const PieceType type = level.tray[Index(piece)].piece.type;
    const int floor = same_as[k] >= 0 ? placement.at[Index(movable[Index(same_as[k])])] : -2;
    // In the tray (only if an identical earlier piece is in the tray too, so the pair isn't tried twice).
    if (floor < 0) {
      placement.at[Index(piece)] = -1;
      placement.setting[Index(piece)] = level.tray[Index(piece)].setting;
      Pieces(k + 1);
    }
    for (int node : choices[k]) {
      if (stopped) return;
      if (node <= floor) continue;
      const Node& n = level.nodes[Index(node)];
      const bool pair = type == PieceType::TwinGate && n.twin >= 0;
      if (taken[Index(node)] >= 0 || (pair && taken[Index(n.twin)] >= 0)) continue;
      taken[Index(node)] = static_cast<int>(type);
      if (pair) taken[Index(n.twin)] = static_cast<int>(type);
      placement.at[Index(piece)] = node;
      const int settings = HasSetting(type) ? 2 : 1;
      for (int s = 0; s < settings && !stopped; ++s) {
        placement.setting[Index(piece)] = HasSetting(type) ? s : level.tray[Index(piece)].setting;
        Pieces(k + 1);
      }
      taken[Index(node)] = -1;
      if (pair) taken[Index(n.twin)] = -1;
    }
    placement.at[Index(piece)] = -1;
  }

  void Flaps(size_t k) {
    if (stopped) return;
    if (k == taps.size()) {
      if (!visit(placement)) stopped = true;
      return;
    }
    const int node = taps[k];
    const int on = taken[Index(node)];
    if (on >= 0 && (IsRouter(static_cast<PieceType>(on)) ||
                    Role(static_cast<PieceType>(on), level.nodes[Index(node)]) == PieceRole::Trap)) {
      // A router decides instead of the flap, and nothing gets past a trap: the flap doesn't matter.
      placement.flap[Index(node)] = level.nodes[Index(node)].flap;
      Flaps(k + 1);
      return;
    }
    for (int side = 0; side < 2 && !stopped; ++side) {
      placement.flap[Index(node)] = side;
      Flaps(k + 1);
    }
    placement.flap[Index(node)] = level.nodes[Index(node)].flap;
  }
};

}  // namespace

Outcome Simulate(const Level& level, const Placement& placement, bool stop_at_failure) {
  Board board(level);
  board.SetPlacement(placement);
  board.SetStopAtFailure(stop_at_failure);
  board.Start();
  while (board.Running()) board.Tick();
  return board.GetOutcome();
}

void ForEachPlacement(const Level& level, const std::function<bool(const Placement&)>& visit) {
  Search search{level, visit, {}, {}, {}, {}, std::vector<int>(level.nodes.size(), -1), {}, false};
  search.placement.at.assign(level.tray.size(), -1);
  search.placement.setting.assign(level.tray.size(), 0);
  search.placement.flap.assign(level.nodes.size(), -1);
  for (size_t i = 0; i < level.tray.size(); ++i) {
    const TrayPiece& tray_piece = level.tray[i];
    search.placement.setting[i] = tray_piece.setting;
    if (tray_piece.locked) {
      const int type = static_cast<int>(tray_piece.piece.type);
      search.placement.at[i] = tray_piece.at;
      search.taken[Index(tray_piece.at)] = type;
      const int twin = level.nodes[Index(tray_piece.at)].twin;
      if (tray_piece.piece.type == PieceType::TwinGate && twin >= 0) search.taken[Index(twin)] = type;
    }
  }
  for (size_t i = 0; i < level.tray.size(); ++i) {
    const TrayPiece& tray_piece = level.tray[i];
    if (tray_piece.locked) continue;
    int same = -1;
    for (size_t k = 0; k < search.movable.size(); ++k) {
      if (level.tray[Index(search.movable[k])].piece == tray_piece.piece) same = static_cast<int>(k);
    }
    // Only the sockets where the piece does its job: an idle piece changes nothing and a trap can only
    // fail, so a placement with either is never better than the same one without it.
    std::vector<int> fits;
    for (size_t n = 0; n < level.nodes.size(); ++n) {
      const Node& node = level.nodes[n];
      if (node.socket && Role(tray_piece.piece.type, node) == PieceRole::Acts && search.taken[n] < 0)
        fits.push_back(static_cast<int>(n));
    }
    search.movable.push_back(static_cast<int>(i));
    search.same_as.push_back(same);
    search.choices.push_back(std::move(fits));
  }
  for (size_t n = 0; n < level.nodes.size(); ++n) {
    const Node& node = level.nodes[n];
    if (node.kind == NodeKind::Fork && node.flap_tappable) {
      search.taps.push_back(static_cast<int>(n));
      search.placement.flap[n] = node.flap;
    }
  }
  search.Pieces(0);
}

SolveReport Solve(const Level& level, const SolveOptions& options) {
  SolveReport report;
  Board board(level);
  board.SetStopAtFailure(true);
  bool any = false;
  Solution lean, swift;
  ForEachPlacement(level, [&](const Placement& placement) {
    ++report.placements;
    board.SetPlacement(placement);
    board.Start();
    while (board.Running()) board.Tick();
    const Outcome outcome = board.GetOutcome();  // a copy: Stop() clears it
    board.Stop();
    if (outcome.solved) {
      ++report.solutions;
      const Solution solution{placement, outcome.pieces, outcome.ticks};
      if (options.keep) report.kept.push_back(solution);
      if (!any || std::tie(solution.pieces, solution.ticks) < std::tie(lean.pieces, lean.ticks)) lean = solution;
      if (!any || std::tie(solution.ticks, solution.pieces) < std::tie(swift.ticks, swift.pieces)) swift = solution;
      any = true;
    }
    return report.placements < options.max_placements;
  });
  report.pars.solutions = report.solutions;
  if (any) {
    report.pars.pieces = lean.pieces;
    report.pars.ticks = swift.ticks;
    report.pars.lean = lean.placement;
    report.pars.swift = swift.placement;
    report.two_answers = !(lean.ticks == swift.ticks && lean.pieces == swift.pieces);
  }
  return report;
}

HintMove NextHint(const Level& level, const Placement& current, const Placement& solution) {
  HintMove move;
  if (solution.at.size() != level.tray.size() || current.at.size() != level.tray.size()) return move;
  auto spec = [&](int piece) { return level.tray[Index(piece)].piece; };
  // Which current piece already stands where the solution wants one like it (with its setting).
  std::vector<bool> used(level.tray.size(), false);
  std::vector<bool> done(level.tray.size(), false);
  for (size_t s = 0; s < solution.at.size(); ++s) {
    if (solution.at[s] < 0 || level.tray[s].locked) continue;
    for (size_t c = 0; c < current.at.size(); ++c) {
      if (used[c] || level.tray[c].locked || current.at[c] != solution.at[s] ||
          !(spec(static_cast<int>(c)) == spec(static_cast<int>(s))))
        continue;
      const bool setting_ok = !HasSetting(level.tray[s].piece.type) || current.setting[c] == solution.setting[s];
      if (!setting_ok) {
        move.piece = static_cast<int>(c);
        move.node = solution.at[s];
        move.setting = solution.setting[s];
        return move;
      }
      used[c] = true;
      done[s] = true;
      break;
    }
  }
  // The first piece of the solution not yet in place: move an identical piece there, preferably
  // one from the tray or one standing where the solution doesn't want it.
  for (size_t s = 0; s < solution.at.size(); ++s) {
    if (done[s] || solution.at[s] < 0 || level.tray[s].locked) continue;
    int best = -1;
    for (size_t c = 0; c < current.at.size(); ++c) {
      if (used[c] || level.tray[c].locked || !(spec(static_cast<int>(c)) == spec(static_cast<int>(s)))) continue;
      if (best < 0 || (current.at[c] < 0 && current.at[Index(best)] >= 0)) best = static_cast<int>(c);
    }
    if (best >= 0) {
      move.piece = best;
      move.node = solution.at[s];
      move.setting = solution.setting[s];
      return move;
    }
  }
  // Every wanted piece is in place: take away one the solution doesn't use.
  for (size_t c = 0; c < current.at.size(); ++c) {
    if (!used[c] && !level.tray[c].locked && current.at[c] >= 0) {
      move.piece = static_cast<int>(c);
      move.node = -1;
      return move;
    }
  }
  for (size_t n = 0; n < solution.flap.size() && n < current.flap.size(); ++n) {
    if (solution.flap[n] >= 0 && current.flap[n] != solution.flap[n]) {
      bool covered = false;  // a router on the fork makes the flap irrelevant
      for (size_t c = 0; c < current.at.size(); ++c)
        covered |= current.at[c] == static_cast<int>(n) && IsRouter(level.tray[c].piece.type);
      if (covered) continue;
      move.flap_node = static_cast<int>(n);
      move.flap = solution.flap[n];
      return move;
    }
  }
  return move;
}

}  // namespace threadlock
