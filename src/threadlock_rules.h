#pragma once

#include <array>
#include <cstdint>
#include <vector>

// Threadlock's rules: a tiny, deterministic flow network. No engine includes, so the tests run
// without a window or GPU.
//
// The board is a graph: nodes (source, junctions, cups) joined by segments measured in cells.
// Liquid moves in blobs. Each Tick() a blob moves one cell, enters the node at the end of its
// segment, or leaves a node onto the next segment. Integers only: the same pieces always give
// the same run (like the cycle-based sims of SpaceChem / Opus Magnum). The view only draws what
// Tick() reports.
namespace threadlock {

enum class Liquid : uint8_t { Blue, Amber, Mixed, Mud };
enum class Piece : uint8_t { None, Filter, Turnstile, Tank };
inline constexpr int kPieceKinds = 3;  // Filter, Turnstile, Tank: one of each in the tray

enum class NodeKind : uint8_t {
  Source,  // pours Mixed blobs onto outs[0]
  Pass,    // in -> outs[0]
  Split,   // a Mixed blob splits in two: outs[0] gets the blue half, outs[1] the amber half (Mud without a filter)
  Cross,   // lanes cross: each in-segment has its own out-segment; blobs of two lanes inside at once = mud
  Cup,     // the end: collects blobs, wants one liquid
};

struct Node {
  NodeKind kind = NodeKind::Pass;
  bool socket = false;            // a piece can be placed here
  std::vector<int> outs;          // out-segments (Cross: one per lane, matching `ins`)
  std::vector<int> ins;           // Cross only: in-segments, lane i = ins[i] -> outs[i]
  Liquid wants = Liquid::Blue;    // Cup only
  int dwell = 1;                  // ticks a blob spends inside (a crossing is as wide as a blob)
};

struct Segment {
  int from = 0;   // node
  int to = 0;     // node
  int cells = 1;  // its length: a blob needs this many ticks to cross it
};

struct Level {
  std::vector<Node> nodes;
  std::vector<Segment> segments;
  int source = 0;          // node
  int pour_every = 12;     // ticks between blobs
  int pour_count = 6;      // blobs the source pours
  int cup_needs = 6;       // blobs each cup must get
  int tank_hold = 6;       // extra ticks a tank keeps a blob
};

// Level 1, "Two Cups": the source pours a blue+amber mix; the cups are swapped so the two
// streams must cross. Node and segment ids are named in TwoCups:: for the view.
struct TwoCups {
  enum NodeId { kSource, kSplit, kTank, kCross, kCupBlue, kCupAmber };
  enum SegmentId { kFeed, kBlueToCross, kAmberToTank, kTankToCross, kCrossToBlue, kCrossToAmber };
  static Level Make();
};

enum class EventType : uint8_t {
  EnterSegment,  // a blob started along `segment` (its liquid in `liquid`)
  EnterNode,     // a blob reached `node` (by `segment`)
  Collide,       // two lanes met in `node`: everything in it turned to mud
  CupFilled,     // a blob landed in cup `node` (`liquid` = what it was)
  Finished,      // the source is empty and every blob has landed
};

struct Event {
  EventType type;
  int blob = -1;
  int node = -1;
  int segment = -1;
  Liquid liquid = Liquid::Mixed;
};

struct Cup {
  int count = 0;
  bool mud = false;  // something it doesn't want landed in it
};

class Board {
 public:
  explicit Board(Level level);

  // ---- Editing (between runs) ----
  // Puts `piece` on socket `node`: it leaves wherever it was, and whatever was on `node` goes back
  // to the tray. False if `node` isn't a socket.
  bool Place(Piece piece, int node);
  void Remove(Piece piece);              // back to the tray
  Piece PieceAt(int node) const { return pieces_[Index(node)]; }
  int NodeOf(Piece piece) const;         // -1 = in the tray

  // ---- Running ----
  void Start();                          // empties the board, cups and source
  const std::vector<Event>& Tick();      // one step; the events stay valid until the next Tick
  bool Running() const { return started_ && !finished_; }
  bool Finished() const { return finished_; }
  bool Solved() const;                   // finished, every cup full and pure
  int Ticks() const { return tick_; }

  const Level& GetLevel() const { return level_; }
  const Cup& CupAt(int node) const { return cups_[Index(node)]; }
  // True while a blob is stuck at the end of `segment`: waiting for a turnstile, or held by a
  // tank in the node there. The view piles its liquid up there.
  bool Blocked(int segment) const;

 private:
  struct Blob {
    int id = 0;
    Liquid liquid = Liquid::Mixed;
    int segment = -1;  // on a segment (cell below), or -1 = inside `node`
    int cell = 0;
    int node = -1;
    int came_from = -1;  // the segment it entered its node by
    int dwell = 0;       // ticks left inside its node
    bool collided = false;
    bool moved = false;    // this tick
    bool waiting = false;  // this tick: stuck at a turnstile
    bool dead = false;     // landed in a cup: removed at the end of the tick
  };

  static size_t Index(int i) { return static_cast<size_t>(i); }
  bool CellFree(int segment, int cell) const;
  bool NodeBusy(int node) const;  // someone inside (a turnstile lets one at a time)
  void EnterNode(Blob& blob, int node);
  // False: the way out is full, try again next tick. A split adds its second half to `born`.
  bool LeaveNode(Blob& blob, std::vector<Blob>& born);
  void StartOn(Blob& blob, int segment);
  int NewBlobId() { return next_id_++; }

  Level level_;
  std::vector<Piece> pieces_;  // per node
  std::vector<Cup> cups_;      // per node (only cups used)
  std::vector<Blob> blobs_;
  std::vector<Event> events_;
  int tick_ = 0;
  int poured_ = 0;
  int next_pour_ = 1;
  int next_id_ = 0;
  bool started_ = false;
  bool finished_ = false;
};

}  // namespace threadlock
