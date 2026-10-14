#include "threadlock_rules.h"

#include <algorithm>
#include <utility>

namespace threadlock {

Level TwoCups::Make() {
  Level level;
  level.nodes.resize(6);
  level.nodes[kSource] = {.kind = NodeKind::Source, .outs = {kFeed}};
  level.nodes[kSplit] = {.kind = NodeKind::Split, .socket = true, .outs = {kBlueToCross, kAmberToTank}};
  level.nodes[kTank] = {.kind = NodeKind::Pass, .socket = true, .outs = {kTankToCross}};
  level.nodes[kCross] = {.kind = NodeKind::Cross,
                         .socket = true,
                         .outs = {kCrossToBlue, kCrossToAmber},
                         .ins = {kBlueToCross, kTankToCross},
                         .dwell = 3};
  level.nodes[kCupBlue] = {.kind = NodeKind::Cup, .wants = Liquid::Blue};
  level.nodes[kCupAmber] = {.kind = NodeKind::Cup, .wants = Liquid::Amber};
  // Both lanes take the same time from the split to the crossing (31 cells; the amber lane's 15 +
  // 1 tick in the tank socket + 15), so without help the halves of a pour meet there.
  level.segments.resize(6);
  level.segments[kFeed] = {kSource, kSplit, 7};
  level.segments[kBlueToCross] = {kSplit, kCross, 31};
  level.segments[kAmberToTank] = {kSplit, kTank, 15};
  level.segments[kTankToCross] = {kTank, kCross, 15};
  level.segments[kCrossToBlue] = {kCross, kCupBlue, 20};
  level.segments[kCrossToAmber] = {kCross, kCupAmber, 20};
  level.source = kSource;
  return level;
}

Board::Board(Level level)
    : level_(std::move(level)), pieces_(level_.nodes.size(), Piece::None), cups_(level_.nodes.size()) {}

bool Board::Place(Piece piece, int node) {
  if (piece == Piece::None || node < 0 || Index(node) >= level_.nodes.size() || !level_.nodes[Index(node)].socket) {
    return false;
  }
  Remove(piece);
  pieces_[Index(node)] = piece;  // whatever was here goes back to the tray
  return true;
}

void Board::Remove(Piece piece) {
  for (Piece& at : pieces_) {
    if (at == piece) at = Piece::None;
  }
}

int Board::NodeOf(Piece piece) const {
  for (size_t i = 0; i < pieces_.size(); ++i) {
    if (pieces_[i] == piece) return static_cast<int>(i);
  }
  return -1;
}

void Board::Start() {
  blobs_.clear();
  events_.clear();
  std::fill(cups_.begin(), cups_.end(), Cup{});
  tick_ = 0;
  poured_ = 0;
  next_pour_ = 1;
  next_id_ = 0;
  started_ = true;
  finished_ = false;
}

bool Board::Solved() const {
  if (!finished_) return false;
  for (size_t i = 0; i < level_.nodes.size(); ++i) {
    if (level_.nodes[i].kind != NodeKind::Cup) continue;
    if (cups_[i].mud || cups_[i].count != level_.cup_needs) return false;
  }
  return true;
}

bool Board::Blocked(int segment) const {
  const Segment& seg = level_.segments[Index(segment)];
  for (const Blob& blob : blobs_) {
    if (blob.segment == segment && blob.waiting) return true;
    if (blob.segment < 0 && blob.node == seg.to && blob.came_from == segment && blob.dwell >= 2 &&
        pieces_[Index(seg.to)] == Piece::Tank) {
      return true;
    }
  }
  return false;
}

bool Board::CellFree(int segment, int cell) const {
  for (const Blob& blob : blobs_) {
    if (!blob.dead && blob.segment == segment && blob.cell == cell) return false;
  }
  return true;
}

bool Board::NodeBusy(int node) const {
  for (const Blob& blob : blobs_) {
    if (!blob.dead && blob.segment < 0 && blob.node == node) return true;
  }
  return false;
}

void Board::StartOn(Blob& blob, int segment) {
  blob.segment = segment;
  blob.cell = 0;
  blob.node = -1;
  blob.moved = true;
  events_.push_back({.type = EventType::EnterSegment, .blob = blob.id, .segment = segment, .liquid = blob.liquid});
}

void Board::EnterNode(Blob& blob, int node) {
  blob.came_from = blob.segment;
  blob.segment = -1;
  blob.node = node;
  blob.moved = true;
  const Piece piece = pieces_[Index(node)];
  blob.dwell = level_.nodes[Index(node)].dwell + (piece == Piece::Tank ? level_.tank_hold : 0);
  events_.push_back(
      {.type = EventType::EnterNode, .blob = blob.id, .node = node, .segment = blob.came_from, .liquid = blob.liquid});
  const Node& n = level_.nodes[Index(node)];
  if (n.kind == NodeKind::Cup) {
    Cup& cup = cups_[Index(node)];
    ++cup.count;
    if (blob.liquid != n.wants) cup.mud = true;
    blob.dead = true;
    events_.push_back({.type = EventType::CupFilled, .blob = blob.id, .node = node, .liquid = blob.liquid});
  }
}

bool Board::LeaveNode(Blob& blob, std::vector<Blob>& born) {
  const Node& n = level_.nodes[Index(blob.node)];
  switch (n.kind) {
    case NodeKind::Pass:
    case NodeKind::Source:
      if (!CellFree(n.outs[0], 0)) return false;
      StartOn(blob, n.outs[0]);
      return true;
    case NodeKind::Cross:
      for (size_t lane = 0; lane < n.ins.size(); ++lane) {
        if (n.ins[lane] != blob.came_from) continue;
        if (!CellFree(n.outs[lane], 0)) return false;
        StartOn(blob, n.outs[lane]);
        return true;
      }
      return false;
    case NodeKind::Split: {
      if (!CellFree(n.outs[0], 0) || !CellFree(n.outs[1], 0)) return false;
      Liquid first = blob.liquid, second = blob.liquid;
      if (blob.liquid == Liquid::Mixed) {
        const bool filter = pieces_[Index(blob.node)] == Piece::Filter;
        first = filter ? Liquid::Blue : Liquid::Mud;  // unsorted, each half is a muddy mix
        second = filter ? Liquid::Amber : Liquid::Mud;
      }
      Blob half = blob;
      half.id = NewBlobId();
      half.liquid = second;
      blob.liquid = first;
      StartOn(blob, n.outs[0]);
      StartOn(half, n.outs[1]);
      born.push_back(half);
      return true;
    }
    case NodeKind::Cup:
      return false;
  }
  return false;
}

const std::vector<Event>& Board::Tick() {
  events_.clear();
  if (!started_ || finished_) return events_;
  ++tick_;
  for (Blob& blob : blobs_) {
    blob.moved = false;
    blob.waiting = false;
  }

  // 1. Blobs inside nodes: count down, then out onto the next segment.
  std::vector<Blob> born;
  for (Blob& blob : blobs_) {
    if (blob.segment >= 0 || blob.dead) continue;
    if (blob.dwell > 0) --blob.dwell;
    if (blob.dwell == 0) LeaveNode(blob, born);
  }
  for (Blob& blob : born) blobs_.push_back(blob);

  // 2. Blobs on segments, front first so a queue moves together: one cell on, or into the node.
  std::vector<size_t> order;
  for (size_t i = 0; i < blobs_.size(); ++i) {
    if (blobs_[i].segment >= 0 && !blobs_[i].moved) order.push_back(i);
  }
  std::sort(order.begin(), order.end(), [this](size_t a, size_t b) {
    const Blob& x = blobs_[a];
    const Blob& y = blobs_[b];
    return x.segment != y.segment ? x.segment < y.segment : x.cell > y.cell;
  });
  for (size_t i : order) {
    Blob& blob = blobs_[i];
    const Segment& seg = level_.segments[Index(blob.segment)];
    if (blob.cell < seg.cells - 1) {
      if (CellFree(blob.segment, blob.cell + 1)) ++blob.cell;
      continue;
    }
    if (pieces_[Index(seg.to)] == Piece::Turnstile && NodeBusy(seg.to)) {
      blob.waiting = true;  // one at a time
      continue;
    }
    EnterNode(blob, seg.to);
  }

  // 3. The source pours.
  const int feed = level_.nodes[Index(level_.source)].outs[0];
  if (poured_ < level_.pour_count && tick_ >= next_pour_ && CellFree(feed, 0)) {
    Blob blob;
    blob.id = NewBlobId();
    blob.liquid = Liquid::Mixed;
    blob.node = level_.source;
    StartOn(blob, feed);
    blobs_.push_back(blob);
    ++poured_;
    next_pour_ = tick_ + level_.pour_every;
  }

  // 4. Crossings: two lanes inside at once is a race. Everything in there turns to mud.
  for (size_t n = 0; n < level_.nodes.size(); ++n) {
    if (level_.nodes[n].kind != NodeKind::Cross) continue;
    int lane = -1;
    bool race = false;
    for (const Blob& blob : blobs_) {
      if (blob.dead || blob.segment >= 0 || blob.node != static_cast<int>(n)) continue;
      if (lane >= 0 && blob.came_from != lane) race = true;
      lane = blob.came_from;
    }
    if (!race) continue;
    bool fresh = false;
    for (Blob& blob : blobs_) {
      if (blob.dead || blob.segment >= 0 || blob.node != static_cast<int>(n) || blob.collided) continue;
      blob.collided = true;
      blob.liquid = Liquid::Mud;
      fresh = true;
    }
    if (fresh) events_.push_back({.type = EventType::Collide, .node = static_cast<int>(n), .liquid = Liquid::Mud});
  }

  std::erase_if(blobs_, [](const Blob& blob) { return blob.dead; });
  if (poured_ == level_.pour_count && blobs_.empty()) {
    finished_ = true;
    events_.push_back({.type = EventType::Finished});
  }
  return events_;
}

}  // namespace threadlock
