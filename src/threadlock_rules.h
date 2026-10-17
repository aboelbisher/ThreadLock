#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Threadlock's rules (v3, DESIGN.md §2-4): a deterministic flow network. No engine includes, so the
// tests, the solver and the level tool run without a window or GPU.
//
// The board is a graph: nodes (spouts, forks, junctions, pipe sockets, cups, drains, and machine
// parts: turning racks, mills, gates) joined by one-way pipes measured in cells. Liquid moves in
// beads: a pure drop, or a blend of two drops stuck together. Each Tick() every bead moves one cell
// (or into / out of a node) if the way is free; a bead that can't move waits and the ones behind it
// queue up. Integers only: the same pieces always give the same run (like the cycle-based sims of
// SpaceChem / Opus Magnum). The view only draws what the board reports.
//
// One speed everywhere: a pipe's cell count comes from its drawn length (kCellLength units a
// cell), and a node's dwell from its size, so a longer pipe on screen is a later arrival.
//
// Sockets are open: any piece goes in any socket (a fork, a pipe socket, a crossing or a merge).
// Where the socket gives a piece what it needs, it does its job; elsewhere it sits idle, and in a
// few places it's a trap that waits forever (Role, DESIGN.md §3).
namespace threadlock {

// ---- Liquids ----

enum class Color : uint8_t { None, Blue, Amber, Green, Rose, Pearl, Ink, Mud };
inline constexpr int kColorCount = 8;

// What one cell of pipe holds: a pure drop (b == None), or a blend of two drops stuck together
// (a separator pulls them apart). Blends are kept in a fixed order (a < b) so equal blends compare
// equal. Nothing is created or lost: a blend counts as two drops in a cup.
struct Bead {
  Color a = Color::None;
  Color b = Color::None;

  static Bead Pure(Color c) { return {c, Color::None}; }
  static Bead Blend(Color x, Color y);
  bool IsBlend() const { return b != Color::None; }
  int Drops() const { return IsBlend() ? 2 : 1; }
  bool Has(Color c) const { return a == c || b == c; }
  bool IsMud() const { return a == Color::Mud; }
  Bead Muddied() const { return IsBlend() ? Bead{Color::Mud, Color::Mud} : Pure(Color::Mud); }
  bool operator==(const Bead&) const = default;
};

// What a mixer makes of two drops: blue + amber = green, green + rose = pearl, a colour with
// itself stays, anything else is mud.
Color Mix(Color x, Color y);
const char* ColorName(Color color);                  // "blue"
char ColorLetter(Color color);                       // 'B' (as in level files)
bool ParseColor(std::string_view text, Color& out);  // "blue" or "B"
std::string BeadText(const Bead& bead);              // "B", "B+A"
bool ParseBead(std::string_view text, Bead& out);
// A spout's sequence: beads separated by spaces, "*n" repeats one ("B+A*6", "B A B A").
bool ParseBeads(std::string_view text, std::vector<Bead>& out);
std::string BeadsText(const std::vector<Bead>& beads);

// ---- Pieces ----

// What each piece does where it acts (the full table of where that is: Role).
enum class PieceType : uint8_t {
  Separator,  // sorts by colour: splits a blend into its colours, a pure drop goes to its colour's side
  Turnstile,  // one drop inside at a time; its arrow picks who goes first
  TankShort,  // holds each drop 4 extra ticks
  TankTall,   // 8 extra ticks
  Flipper,    // left, right, left, right... starting on the side it shows
  Overflow,   // the first N drops go its way, the rest the other way
  Bridge,     // the lanes of a crossing don't touch (no waiting, no mud)
  Mixer,      // waits for one drop from each way in, mixes them
  TwinGate,   // holds two ways in until both have a drop, then both go together
};
inline constexpr int kPieceTypes = 9;

struct Piece {
  PieceType type = PieceType::Separator;
  int n = 0;              // Overflow: how many go its way
  Color a = Color::Blue;  // Separator: the colours it sorts (a to side 0 unless swapped)
  Color b = Color::Amber;
  bool operator==(const Piece&) const = default;
};

const char* PieceName(PieceType type);  // "Separator"
const char* PieceKey(PieceType type);   // "separator" (level files)
bool ParsePieceType(std::string_view text, PieceType& out);
bool HasSetting(PieceType type);  // tapping it changes something
int TankExtra(PieceType type);    // 4 / 8 / 0

// ---- The board's parts ----

enum class NodeKind : uint8_t {
  Spout,  // pours its beads onto its pipe, one every `beat` ticks
  Fork,   // one pipe in, two out; it follows its flap unless a router (separator, flipper, overflow) sits on it
  Pass,   // a socket on a pipe; `twin` links two side by side (a twin pair)
  Cross,  // two lanes cross: lane i = ins[i] -> outs[i]
  Merge,  // two pipes become one
  Cup,    // wants `want`, exactly `count` drops
  Drain,  // takes anything, counts nothing
  Rack,   // a turning rack: bottles (`slots`) step round under the end of its pipe every `period` ticks
  Mill,   // a mill wheel on a pipe: each drop leaving it flips the gates in `gears`
  Gate,   // a gate on a pipe: shut, it lets nothing in
};
const char* NodeKindKey(NodeKind kind);
bool ParseNodeKind(std::string_view text, NodeKind& out);
bool IsSocketKind(NodeKind kind);  // fork, pass, cross, merge: where sockets can be

// What a piece does in a socket (DESIGN.md §3): its job, nothing (it still counts for Lean), or a
// trap (its first drop waits forever for a partner that can't come: THREADLOCKED).
enum class PieceRole : uint8_t { Acts, Idle, Trap };
PieceRole Role(PieceType type, NodeKind kind, bool twin_pair = false);
const char* PieceRoleKey(PieceRole role);  // "acts"

inline constexpr float kCellLength = 10.0f;  // board units a bead moves per tick
inline constexpr int kJunctionDwell = 3;     // ticks to pass a crossing, a merge or a mill

struct Vec2 {
  float x = 0.0f;
  float y = 0.0f;
};

// One bottle of a turning rack: a cup of its own.
struct Slot {
  Color want = Color::Blue;
  int count = 0;
  std::string name;  // the order it fills (look only)
  bool operator==(const Slot&) const = default;
};

struct Node {
  NodeKind kind = NodeKind::Pass;
  std::string id;
  Vec2 at;
  bool socket = false;
  // Spout
  std::vector<Bead> beads;
  int beat = 12;  // ticks between pours
  int start = 1;  // the first pour's tick
  // Fork: the side (index into outs) it sends everything when no router sits on it
  int flap = 0;
  bool flap_tappable = false;
  // Cup
  Color want = Color::Blue;
  int count = 0;
  // Pass: the other half of a twin pair (-1 = none)
  int twin = -1;
  // Rack: a drop arriving at tick t lands in slot (first + t / period) % slots.size()
  std::vector<Slot> slots;
  int period = 4;
  int first = 0;
  // Mill: the gates (nodes) each drop leaving it flips
  std::vector<int> gears;
  // Gate: open at the start
  bool open = true;

  // ---- Derived by Level::Finalize ----
  std::vector<int> ins;   // pipes; Cross: lane i = ins[i] -> outs[i]
  std::vector<int> outs;  // pipes
  int dwell = 1;          // ticks a bead spends inside (Cup / Drain / Rack / Spout: 0)
  float radius = 0.0f;    // drawn size: pipes start and end at this distance from `at`
};

// The role of a piece on this node (a twin gate acts on either half of a twin pair).
inline PieceRole Role(PieceType type, const Node& node) { return Role(type, node.kind, node.twin >= 0); }

struct Pipe {
  int from = 0;           // node
  int to = 0;             // node
  std::vector<Vec2> via;  // bends between the two nodes
  // Shapes (a level file's object after the bends). Segment k runs from point k to k+1 of
  // (from, via..., to).
  int coil_segment = -1;  // wound into a coil of coil_turns turns round its middle: a longer way, so slower
  int coil_turns = 0;
  int drip_segment = -1;  // drops fall through the air here (look only: the timing is the same)

  // ---- Derived by Level::Finalize ----
  std::vector<Vec2> points;  // the drawn centre line: from its node's edge to the next one's
  float length = 0.0f;       // of `points`
  int cells = 1;             // round(length / kCellLength), at least 1
  Vec2 coil_a, coil_b;       // a coil's axis: it starts at coil_a and ends at coil_b
  float coil_start = 0.0f;   // the coil, as distances along `points` (end <= start: none)
  float coil_end = 0.0f;
  float drip_start = 0.0f;  // the drip fall, as distances along `points` (end <= start: none)
  float drip_end = 0.0f;
};

// A coil: `turns` turns of a helix round the axis a -> b, seen from the side (the drops' path, which the
// rules measure: a turn is about 70 long, about 5 cells more than the straight pipe). Starts at a and ends at b,
// kCoilSamples points a turn.
inline constexpr int kCoilSamples = 16;
inline constexpr float kCoilPitch = 18.0f;   // along the axis, per turn
inline constexpr float kCoilRadius = 15.0f;  // across
inline constexpr float kCoilDepth = 6.0f;    // the near side dips this far along the axis
std::vector<Vec2> CoilPoints(Vec2 a, Vec2 b, int turns);
// Whether the coil's stretch from sample k to k + 1 is on its near side (drawn in front).
inline bool CoilFront(int k) {
  const int m = k % kCoilSamples;
  return m >= 4 && m < 12;
}

// A piece in the level's tray, or placed by the level (`at` >= 0). Locked pieces can't be moved
// or changed (repair levels place movable ones).
struct TrayPiece {
  Piece piece;
  int at = -1;
  int setting = 0;
  bool locked = false;
};

enum class LevelType : uint8_t { Build, Repair, Spectacle, Challenge, Daily, Lab };
const char* LevelTypeKey(LevelType type);
bool ParseLevelType(std::string_view text, LevelType& out);

// A placement of the tray's pieces (and of the tappable flaps): what the player sets up, what the
// solver searches and what hints show.
struct Placement {
  std::vector<int> at;       // per tray piece: node, -1 = in the tray
  std::vector<int> setting;  // per tray piece
  std::vector<int> flap;     // per node: side (forks with a tappable flap), else -1
  bool operator==(const Placement&) const = default;
};

// The best answers, found by the solver (tools/threadlock_tool solve): pars for the stars, and
// solutions for hints.
struct Pars {
  int pieces = -1;  // Lean: solved with no more than this many pieces
  int ticks = -1;   // Swift: solved within this many ticks
  Placement lean;   // a solution with `pieces` pieces (the fastest of those)
  Placement swift;  // a solution within `ticks` ticks (the fewest pieces of those)
  int solutions = 0;
  bool Known() const { return pieces >= 0; }
};

struct Level {
  std::string id;    // "1-4"
  std::string name;  // "Two Cups"
  int world = 0;
  int number = 0;  // in its world
  LevelType type = LevelType::Build;
  std::string intro;  // one line shown on the board at the start (a level's teaching sentence)
  std::vector<Node> nodes;
  std::vector<Pipe> pipes;
  std::vector<TrayPiece> tray;
  Pars pars;

  // Computes the derived fields (ins/outs, crossing lanes, dwells, pipe cells) and checks the
  // level. Returns the problems found (empty = good).
  std::vector<std::string> Finalize();
  int NodeIndex(std::string_view id) const;  // -1 if none
  int CupCount() const;                      // cups and rack bottles
  int Drops() const;                         // drops all spouts pour
};

// ---- Running ----

enum class Failure : uint8_t {
  None,
  Mud,       // different colours shared a junction
  WrongCup,  // a cup got a colour it doesn't want
  Spill,     // a cup got more than it wants
  Dry,       // a cup ended short
  Jam,       // nothing can ever move again: THREADLOCKED
};
const char* FailureKey(Failure failure);

enum class EventType : uint8_t {
  Pour,      // spout `node` poured `drop` (`bead`) onto `pipe`
  Enter,     // `drop` entered `node` from `pipe`
  Leave,     // `drop` left `node` onto `pipe` (leaving a mill turns it a notch)
  Split,     // in `node`, `drop` split: `other` is its new half
  Mix,       // in `node`, `drop` and `other` mixed into `bead`
  Collide,   // different colours met in `node`: everything inside turned to mud
  NearMiss,  // different colours passed `node` with 0-1 ticks to spare
  Wait,      // `drop` started waiting at `node` (a lock, a tank, a partner, a shut gate)
  Land,      // `drop` (`bead`) landed in cup / drain / rack `node` (bottle `slot`); `value` = its count after
  Spill,     // cup `node` (or its bottle `slot`) overflowed
  CupFull,   // cup `node` (or its bottle `slot`) got exactly what it wants, pure
  Flip,      // gate `node` flipped at the end of the tick (`value` = 1: now open)
  Turn,      // rack `node` turned: bottle `slot` is under its pipe from this tick on
  Finished,  // every drop has landed (`value` = 1 if solved)
  Jammed,    // nothing can ever move again
};

struct Event {
  EventType type = EventType::Pour;
  int node = -1;
  int pipe = -1;
  int drop = -1;
  int other = -1;
  Bead bead{};
  int value = 0;
  int slot = -1;  // a rack's bottle
};

// Where a drop is: on a pipe (`index` = pipe, `cell`) or inside a node (`index` = node, `cell` =
// how far through it, 0..dwell-1).
struct Loc {
  enum Kind : uint8_t { None, OnPipe, InNode } kind = None;
  int index = -1;
  int cell = 0;
  bool operator==(const Loc&) const = default;
};

struct Drop {
  int id = -1;
  Bead bead{};
  Loc loc;
  Loc prev;       // where it was at the start of this tick (the view interpolates)
  int dwell = 0;  // ticks left inside its node
  int dwell_total = 0;
  int lane_in = -1;   // which of the node's ins it came by
  int lane_out = -1;  // which of the node's outs it leaves by (-2 = splits to both)
  int entered = 0;    // the tick it entered its node
  bool alive = false;
  bool moved = false;    // this tick
  bool waiting = false;  // stuck this tick (the view piles liquid up)
};

struct CupState {
  int count = 0;
  bool impure = false;
  bool spilled = false;
  int landed_tick = -1;  // the last landing
};

struct Outcome {
  bool over = false;
  bool solved = false;
  bool jammed = false;
  Failure failure = Failure::None;  // the first one, in time
  int fail_node = -1;               // where it happened
  int fail_slot = -1;               // which bottle, when it happened at a rack
  int fail_tick = -1;               // when
  int ticks = 0;                    // when the run ended
  int pieces = 0;                   // movable pieces on the board (Lean)
};

// After a jam: who waits for whom, for the THREADLOCKED picture.
struct JamReport {
  std::vector<int> stuck_nodes;  // nodes with a drop stuck inside or at their entrance
  std::vector<int> cycle;        // nodes around a circle of waiting, in order (empty = none)
  // A node whose drop waits for something that never comes: a mixer's or twin gate's partner (a
  // trap: one way in), or the mill turn that would open a shut gate.
  int lonely = -1;
};

// ---- Geometry (for drawing) ----

// The point `distance` along a polyline (clamped to its ends).
Vec2 PointAlong(const std::vector<Vec2>& points, float distance);
// Where a drop at `loc` is drawn: the middle of its cell on a pipe, or along its lane through a
// node (in from its pipe's end, through the node's centre, out to the next pipe's start).
Vec2 DropPoint(const Level& level, const Drop& drop, const Loc& loc);

class Board {
 public:
  explicit Board(const Level& level);  // the level must outlive the board

  const Level& GetLevel() const { return *level_; }

  // ---- Editing (between runs) ----
  int PieceCount() const { return static_cast<int>(pieces_.size()); }
  const TrayPiece& PieceInfo(int piece) const { return level_->tray[Index(piece)]; }
  int PieceNode(int piece) const { return pieces_[Index(piece)].at; }  // -1 = in the tray
  int PieceAt(int node) const { return node_piece_[Index(node)]; }     // -1 = none
  bool Locked(int piece) const { return level_->tray[Index(piece)].locked; }
  PieceRole RoleOf(int piece) const;  // what it does where it is (Acts in the tray)
  bool CanPlace(int piece, int node) const;
  // Puts `piece` on `node` (it leaves wherever it was); a movable piece already there goes back
  // to the tray. False if `node` isn't a free socket (or one a locked piece holds). A twin gate on
  // either half of a twin pair takes both halves.
  bool Place(int piece, int node);
  void Unplace(int piece);  // back to the tray
  int Setting(int piece) const { return pieces_[Index(piece)].setting; }
  void SetSetting(int piece, int setting);
  int Flap(int node) const { return flaps_[Index(node)]; }
  bool FlapTappable(int node) const;  // a fork with a handle and no router on it
  bool FlapInCharge(int node) const;  // a fork with no router on it: its flap routes
  void SetFlap(int node, int side);
  int PlacedCount() const;  // movable pieces on the board
  Placement GetPlacement() const;
  void SetPlacement(const Placement& placement);  // ignores what doesn't fit
  void ClearPieces();  // every movable piece back to the tray (preplaced ones to their place)

  // ---- Running ----
  void Start();                      // empties the board, cups and spouts
  void Stop();                       // back to editing
  const std::vector<Event>& Tick();  // one step; the events stay valid until the next Tick
  bool Running() const { return started_ && !outcome_.over; }
  bool Started() const { return started_; }
  const Outcome& GetOutcome() const { return outcome_; }
  int Ticks() const { return tick_; }
  // Stops at the first failure (the solver doesn't need to watch the rest).
  void SetStopAtFailure(bool stop) { stop_at_failure_ = stop; }

  // ---- For the view ----
  const std::vector<Drop>& Drops() const { return drops_; }  // dead ones have alive == false
  const CupState& Cup(int node) const { return cups_[Index(node)]; }
  const CupState& Bottle(int rack, int slot) const { return bottles_[Index(bottle_offset_[Index(rack)] + slot)]; }
  int RackSlot(int rack) const;  // the bottle under the rack's pipe now
  bool GateOpen(int gate) const;
  int MillTurns(int mill) const { return started_ ? mill_turns_[Index(mill)] : 0; }
  int Poured(int spout) const { return poured_[Index(spout)]; }
  int FlipperNext(int node) const { return flipper_next_[Index(node)]; }
  int OverflowPassed(int node) const { return overflow_passed_[Index(node)]; }
  int InsideCount(int node) const { return static_cast<int>(inside_[Index(node)].size()); }
  bool Waiting(int pipe) const;  // a drop is stuck at the end of `pipe`
  JamReport Jam() const;
  // Where the route of a drop in `node` goes (index into outs); -1 if not decided / leaving both ways.
  int Route(const Drop& drop) const { return drop.lane_out; }

 private:
  struct PieceState {
    int at = -1;
    int setting = 0;
  };

  static size_t Index(int i) { return static_cast<size_t>(i); }
  int PieceTypeAt(int node) const;  // PieceType as int, -1 = none
  bool Is(int node, PieceType type) const { return PieceTypeAt(node) == static_cast<int>(type); }
  bool Routes(int node) const;  // a separator, flipper or overflow sits on it
  bool Shared(int node) const;  // a crossing or merge where two colours inside at once make mud
  int NewDrop(const Bead& bead);
  int& Cell(int pipe, int cell) { return cells_[Index(cell_offset_[Index(pipe)] + cell)]; }
  int CellAt(int pipe, int cell) const { return cells_[Index(cell_offset_[Index(pipe)] + cell)]; }
  int Head(int pipe) const { return CellAt(pipe, level_->pipes[Index(pipe)].cells - 1); }  // drop at its end

  bool Admits(int node, int lane) const;
  void EnterNode(int d, int node, int lane);
  bool TryLeave(int d);
  void LeaveOnto(int d, int pipe);
  void Land(int d, int node);
  int Decide(int node, const Drop& drop, int lane);  // the route (out side) of a drop entering `node`
  void CheckCollisions();
  void Fail(Failure failure, int node, int slot = -1);
  void Finish(bool jammed);
  bool Frozen() const;
  int NextArrival(int pipe) const;  // the drop that will next reach the end of `pipe` (-1 = none can)

  const Level* level_;
  std::vector<PieceState> pieces_;
  std::vector<int> node_piece_;  // per node: tray index or -1
  std::vector<int> flaps_;       // per node

  // Run state
  std::vector<Drop> drops_;
  std::vector<int> free_drops_;
  std::vector<int> cells_;                // every pipe's cells, flattened: drop slot or -1
  std::vector<int> cell_offset_;          // per pipe: its first cell in cells_
  std::vector<std::vector<int>> inside_;  // per node: drop slots inside, in entry order
  std::vector<CupState> cups_;
  std::vector<CupState> bottles_;   // every rack's bottles, flattened
  std::vector<int> bottle_offset_;  // per node: its first bottle in bottles_
  std::vector<uint8_t> gate_open_;  // per node
  std::vector<int> pending_flips_;  // per node: flips this tick (applied at its end)
  std::vector<int> mill_turns_;     // per node
  std::vector<int> poured_, next_pour_;
  std::vector<int> flipper_next_, overflow_passed_;
  std::vector<std::array<int, 2>> mixer_slot_;
  std::vector<int> last_exit_tick_;
  std::vector<Bead> last_exit_bead_;
  std::vector<Event> events_;
  Outcome outcome_;
  int tick_ = 0;
  int next_id_ = 0;
  bool started_ = false;
  bool moved_any_ = false;
  bool stop_at_failure_ = false;
};

}  // namespace threadlock
