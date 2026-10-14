// Tests for Threadlock's rules (threadlock_rules.h): level 1 with every way of placing the pieces
// that matters, the tray, blocking, and determinism.
#include <cstdio>
#include <vector>

#include "../src/threadlock_rules.h"

using namespace threadlock;

static int g_failures = 0;
#define CHECK(cond)                                             \
  do {                                                          \
    if (!(cond)) {                                              \
      std::printf("  FAILED: %s (line %d)\n", #cond, __LINE__); \
      ++g_failures;                                             \
    }                                                           \
  } while (0)

struct RunResult {
  bool solved = false;
  int ticks = 0;
  int collisions = 0;
  Cup blue, amber;
  std::vector<Event> events;
};

static RunResult Run(Board& board) {
  RunResult result;
  board.Start();
  for (int i = 0; i < 2000 && !board.Finished(); ++i) {
    for (const Event& e : board.Tick()) {
      if (e.type == EventType::Collide) ++result.collisions;
      result.events.push_back(e);
    }
  }
  result.solved = board.Solved();
  result.ticks = board.Ticks();
  result.blue = board.CupAt(TwoCups::kCupBlue);
  result.amber = board.CupAt(TwoCups::kCupAmber);
  return result;
}

static void TestNoPiecesIsMud() {
  Board board(TwoCups::Make());
  const RunResult r = Run(board);
  CHECK(board.Finished() && !r.solved);
  CHECK(r.blue.mud && r.amber.mud);
  CHECK(r.blue.count == 6 && r.amber.count == 6);  // nothing is lost, it's just mud
}

static void TestFilterOnlyRacesAtTheCrossing() {
  Board board(TwoCups::Make());
  CHECK(board.Place(Piece::Filter, TwoCups::kSplit));
  const RunResult r = Run(board);
  CHECK(!r.solved);
  CHECK(r.collisions == 6);  // every pour's halves meet
  CHECK(r.blue.mud && r.amber.mud);
}

static void TestFilterAndTurnstileSolves() {
  Board board(TwoCups::Make());
  board.Place(Piece::Filter, TwoCups::kSplit);
  board.Place(Piece::Turnstile, TwoCups::kCross);
  const RunResult r = Run(board);
  CHECK(r.solved);
  CHECK(r.collisions == 0);
  CHECK(r.blue.count == 6 && !r.blue.mud && r.amber.count == 6 && !r.amber.mud);
}

static void TestFilterAndTankSolves() {
  Board board(TwoCups::Make());
  board.Place(Piece::Filter, TwoCups::kSplit);
  board.Place(Piece::Tank, TwoCups::kTank);
  const RunResult r = Run(board);
  CHECK(r.solved);
  CHECK(r.collisions == 0);
}

static void TestAllThreeSolves() {
  Board board(TwoCups::Make());
  board.Place(Piece::Filter, TwoCups::kSplit);
  board.Place(Piece::Tank, TwoCups::kTank);
  board.Place(Piece::Turnstile, TwoCups::kCross);
  CHECK(Run(board).solved);
}

static void TestWrongSocketsDontHelp() {
  {
    Board board(TwoCups::Make());  // a tank at the crossing holds both lanes there together
    board.Place(Piece::Filter, TwoCups::kSplit);
    board.Place(Piece::Tank, TwoCups::kCross);
    const RunResult r = Run(board);
    CHECK(!r.solved && r.collisions == 6);
  }
  {
    Board board(TwoCups::Make());  // a turnstile on the amber lane alone never has to choose
    board.Place(Piece::Filter, TwoCups::kSplit);
    board.Place(Piece::Turnstile, TwoCups::kTank);
    CHECK(!Run(board).solved);
  }
  {
    Board board(TwoCups::Make());  // no filter: the crossing is fixed but the mix is never sorted
    board.Place(Piece::Turnstile, TwoCups::kCross);
    board.Place(Piece::Filter, TwoCups::kTank);
    const RunResult r = Run(board);
    CHECK(!r.solved && r.collisions == 0 && r.blue.mud);
  }
}

static void TestTray() {
  Board board(TwoCups::Make());
  CHECK(!board.Place(Piece::Filter, TwoCups::kSource));  // not a socket
  CHECK(!board.Place(Piece::Filter, TwoCups::kCupBlue));
  CHECK(board.Place(Piece::Filter, TwoCups::kSplit));
  CHECK(board.NodeOf(Piece::Filter) == TwoCups::kSplit);
  CHECK(board.Place(Piece::Filter, TwoCups::kCross));  // moves, one of each
  CHECK(board.PieceAt(TwoCups::kSplit) == Piece::None && board.NodeOf(Piece::Filter) == TwoCups::kCross);
  CHECK(board.Place(Piece::Tank, TwoCups::kCross));  // takes the socket, the filter goes back
  CHECK(board.NodeOf(Piece::Filter) == -1 && board.PieceAt(TwoCups::kCross) == Piece::Tank);
  board.Remove(Piece::Tank);
  CHECK(board.NodeOf(Piece::Tank) == -1);
}

static void TestTurnstileMakesAmberWait() {
  Board board(TwoCups::Make());
  board.Place(Piece::Filter, TwoCups::kSplit);
  board.Place(Piece::Turnstile, TwoCups::kCross);
  board.Start();
  bool amber_blocked = false, blue_blocked = false;
  for (int i = 0; i < 2000 && !board.Finished(); ++i) {
    board.Tick();
    amber_blocked |= board.Blocked(TwoCups::kTankToCross);
    blue_blocked |= board.Blocked(TwoCups::kBlueToCross);
  }
  CHECK(amber_blocked);  // waiting for the blue half to turn through
  CHECK(!blue_blocked);  // blue goes first and never stops
}

static void TestTankHoldsAndTimings() {
  Board board(TwoCups::Make());
  board.Place(Piece::Filter, TwoCups::kSplit);
  board.Place(Piece::Tank, TwoCups::kTank);
  board.Start();
  int first_split = -1, first_blue_cross = -1, first_amber_cross = -1;
  bool held = false;
  for (int i = 0; i < 2000 && !board.Finished(); ++i) {
    for (const Event& e : board.Tick()) {
      if (e.type != EventType::EnterNode) continue;
      if (e.node == TwoCups::kSplit && first_split < 0) first_split = board.Ticks();
      if (e.node == TwoCups::kCross && e.liquid == Liquid::Blue && first_blue_cross < 0) first_blue_cross = board.Ticks();
      if (e.node == TwoCups::kCross && e.liquid == Liquid::Amber && first_amber_cross < 0) first_amber_cross = board.Ticks();
    }
    held |= board.Blocked(TwoCups::kAmberToTank);
  }
  CHECK(held);
  CHECK(first_split == 8);                           // poured at tick 1, 7 cells of feed
  CHECK(first_blue_cross == first_split + 32);       // 1 tick in the split + 31 cells
  CHECK(first_amber_cross == first_blue_cross + 6);  // the tank's hold
}

static void TestDeterministic() {
  Board a(TwoCups::Make()), b(TwoCups::Make());
  a.Place(Piece::Filter, TwoCups::kSplit);
  b.Place(Piece::Filter, TwoCups::kSplit);
  const RunResult ra = Run(a), rb = Run(b);
  CHECK(ra.ticks == rb.ticks && ra.events.size() == rb.events.size());
  bool same = ra.events.size() == rb.events.size();
  for (size_t i = 0; same && i < ra.events.size(); ++i) {
    same = ra.events[i].type == rb.events[i].type && ra.events[i].blob == rb.events[i].blob &&
           ra.events[i].node == rb.events[i].node && ra.events[i].segment == rb.events[i].segment &&
           ra.events[i].liquid == rb.events[i].liquid;
  }
  CHECK(same);
  const RunResult again = Run(a);  // Start() really resets
  CHECK(again.ticks == ra.ticks && again.events.size() == ra.events.size());
}

int main() {
  TestNoPiecesIsMud();
  TestFilterOnlyRacesAtTheCrossing();
  TestFilterAndTurnstileSolves();
  TestFilterAndTankSolves();
  TestAllThreeSolves();
  TestWrongSocketsDontHelp();
  TestTray();
  TestTurnstileMakesAmberWait();
  TestTankHoldsAndTimings();
  TestDeterministic();
  if (g_failures == 0) std::printf("All rules tests passed.\n");
  return g_failures == 0 ? 0 : 1;
}
