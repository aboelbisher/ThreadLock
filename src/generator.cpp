#include "generator.h"

#include <algorithm>
#include <array>
#include <initializer_list>
#include <utility>
#include <vector>

#include "solver.h"

namespace threadlock {
namespace {

// SplitMix64: small, fast, and the same numbers on every device.
class Rng {
 public:
  explicit Rng(uint64_t seed) : state_(seed) {}
  uint64_t Next() {
    uint64_t z = (state_ += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
  }
  int Range(int lo, int hi) {  // inclusive
    return lo + static_cast<int>(Next() % static_cast<uint64_t>(hi - lo + 1));
  }
  bool Chance(float p) { return static_cast<float>(Next() % 10000) < p * 10000.0f; }
  template <typename T>
  T Pick(std::initializer_list<T> items) {
    return *(items.begin() + Range(0, static_cast<int>(items.size()) - 1));
  }

 private:
  uint64_t state_;
};

struct Builder {
  Level level;

  int Add(NodeKind kind, const std::string& id, Vec2 at, bool socket = false) {
    Node node;
    node.kind = kind;
    node.id = id;
    node.at = at;
    node.socket = socket || kind == NodeKind::Pass;
    level.nodes.push_back(std::move(node));
    return static_cast<int>(level.nodes.size()) - 1;
  }
  int Spout(const std::string& id, Vec2 at, std::vector<Bead> beads, int beat, int start = 1) {
    const int n = Add(NodeKind::Spout, id, at);
    level.nodes.back().beads = std::move(beads);
    level.nodes.back().beat = beat;
    level.nodes.back().start = start;
    return n;
  }
  int Cup(const std::string& id, Vec2 at, Color want, int count) {
    const int n = Add(NodeKind::Cup, id, at);
    level.nodes.back().want = want;
    level.nodes.back().count = count;
    return n;
  }
  void Pipe(int from, int to, std::vector<Vec2> via = {}) {
    threadlock::Pipe pipe;
    pipe.from = from;
    pipe.to = to;
    pipe.via = std::move(via);
    level.pipes.push_back(std::move(pipe));
  }
  // A pipe through a bend at `via`, with a pipe socket there instead when `socket`.
  void Through(int from, int to, Vec2 via, bool socket, const std::string& id) {
    if (!socket) {
      Pipe(from, to, {via});
      return;
    }
    const int pass = Add(NodeKind::Pass, id, via, true);
    Pipe(from, pass);
    Pipe(pass, to);
  }
  void Tray(PieceType type, int n = 0) {
    TrayPiece piece;
    piece.piece.type = type;
    piece.piece.n = n;
    level.tray.push_back(piece);
  }
  bool Has(NodeKind kind, bool socket) const {
    return std::any_of(level.nodes.begin(), level.nodes.end(),
                       [&](const Node& node) { return node.kind == kind && node.socket == socket; });
  }
};

std::vector<Bead> Repeat(Bead bead, int count) { return std::vector<Bead>(static_cast<size_t>(count), bead); }

// ---- Templates ----

// Two lanes that cross 1-3 times (DESIGN.md World 1/3: races, the fix that moves, scarce bridges).
Level Braid(Rng& rng, int difficulty) {
  Builder b;
  const int crossings = difficulty <= 1 ? 1 : rng.Range(1, difficulty >= 4 ? 3 : 2);
  const bool blend = rng.Chance(0.5f);
  const int count = rng.Range(4, 7);
  const int beat = rng.Pick({8, 10, 12, 14});
  Color left_color = Color::Blue, right_color = Color::Amber;
  if (rng.Chance(0.5f)) std::swap(left_color, right_color);
  int left = -1, right = -1;
  Vec2 left_at, right_at;
  if (blend) {
    const int s = b.Spout("s", {180, 36}, Repeat(Bead::Blend(Color::Blue, Color::Amber), count), beat);
    const int f = b.Add(NodeKind::Fork, "f", {180, 104}, true);
    b.Pipe(s, f);
    left = right = f;
    left_at = right_at = {180, 104};
    left_color = Color::Blue, right_color = Color::Amber;  // the separator's sides, unswapped
  } else {
    left = b.Spout("sl", {80, 40}, Repeat(Bead::Pure(left_color), count), beat);
    right =
        b.Spout("sr", {280, 40}, Repeat(Bead::Pure(right_color), count), beat, rng.Chance(0.3f) ? rng.Range(1, 3) : 1);
    left_at = {80, 40};
    right_at = {280, 40};
  }
  std::vector<float> ys;
  if (crossings == 1) ys = {300};
  if (crossings == 2) ys = {205, 385};
  if (crossings == 3) ys = {180, 302, 424};
  std::vector<int> xs;
  for (int k = 0; k < crossings; ++k) {
    const bool socket = rng.Chance(difficulty <= 1 ? 0.8f : 0.6f);
    xs.push_back(b.Add(NodeKind::Cross, "x" + std::to_string(k + 1), {180, ys[static_cast<size_t>(k)]}, socket));
  }
  // Between one crossing and the next each lane bulges out to its side by a random amount: the
  // bulges set the lengths, so the timings.
  const float pass_chance = difficulty <= 1 ? 0.35f : 0.3f;
  for (int k = 0; k < crossings; ++k) {
    const float from_y = k == 0 ? (left_at.y + right_at.y) * 0.5f : ys[static_cast<size_t>(k - 1)];
    const float mid = (from_y + ys[static_cast<size_t>(k)]) * 0.5f;
    const float wl = static_cast<float>(rng.Range(45, 130)), wr = static_cast<float>(rng.Range(45, 130));
    const int from_left = k == 0 ? left : xs[static_cast<size_t>(k - 1)];
    const int from_right = k == 0 ? right : xs[static_cast<size_t>(k - 1)];
    b.Through(from_left, xs[static_cast<size_t>(k)], {180 - wl, mid}, rng.Chance(pass_chance),
              "p" + std::to_string(k) + "l");
    b.Through(from_right, xs[static_cast<size_t>(k)], {180 + wr, mid}, rng.Chance(pass_chance),
              "p" + std::to_string(k) + "r");
  }
  // The colours swap sides at every crossing.
  const bool swapped = crossings % 2 == 1;
  Color left_cup = swapped ? right_color : left_color;
  Color right_cup = swapped ? left_color : right_color;
  if (blend && rng.Chance(0.5f)) std::swap(left_cup, right_cup);  // then the separator must be turned
  const int cl = b.Cup("cl", {85, 512}, left_cup, count);
  const int cr = b.Cup("cr", {275, 512}, right_cup, count);
  b.Pipe(xs.back(), cl, {{85, ys.back() + 60}});
  b.Pipe(xs.back(), cr, {{275, ys.back() + 60}});
  // The tray: pieces with a job on this board (any piece goes in any socket; the solver only counts
  // the ones that act), plus the separator for a blend.
  if (blend) b.Tray(PieceType::Separator);
  const int junctions = static_cast<int>(std::count_if(
      b.level.nodes.begin(), b.level.nodes.end(), [](const Node& n) { return n.kind == NodeKind::Cross && n.socket; }));
  const bool passes = b.Has(NodeKind::Pass, true);
  if (junctions > 0) b.Tray(PieceType::Turnstile);
  if (junctions > 1 && difficulty >= 3 && rng.Chance(0.5f)) b.Tray(PieceType::Turnstile);
  if (junctions > 0 && difficulty >= 3 && rng.Chance(0.5f)) b.Tray(PieceType::Bridge);
  if (passes && rng.Chance(0.8f)) b.Tray(PieceType::TankShort);
  if (passes && rng.Chance(0.6f)) b.Tray(PieceType::TankTall);
  return b.level;
}

// One stream shared out between cups (World 2: counting, parity, waste).
Level Shares(Rng& rng, int difficulty) {
  Builder b;
  const Color color = difficulty >= 4 ? rng.Pick({Color::Blue, Color::Amber, Color::Green, Color::Rose})
                                      : rng.Pick({Color::Blue, Color::Amber});
  const int shape = rng.Range(0, 2);
  const int n = rng.Range(shape == 1 ? 6 : 5, 9);
  const int beat = rng.Pick({8, 10, 12});
  const int s = b.Spout("s", {180, 36}, Repeat(Bead::Pure(color), n), beat);
  const int f1 = b.Add(NodeKind::Fork, "f1", {180, 120}, true);
  b.Pipe(s, f1);
  std::vector<int> cups;
  int drained = 0;
  // Random counts: a partition of what reaches the cups.
  auto split = [&](int total, int parts) {
    std::vector<int> counts(static_cast<size_t>(parts), 1);
    for (int left = total - parts; left > 0; --left) ++counts[static_cast<size_t>(rng.Range(0, parts - 1))];
    return counts;
  };
  int forks = 1;
  if (shape == 0) {
    const std::vector<int> counts = split(n, 3);
    const int f2 = b.Add(NodeKind::Fork, "f2", {255, 255}, true);
    cups.push_back(b.Cup("c1", {60, 495}, color, counts[0]));
    b.Pipe(f1, cups.back(), {{60, 175}});
    b.Pipe(f1, f2, {{255, 175}});
    cups.push_back(b.Cup("c2", {190, 495}, color, counts[1]));
    cups.push_back(b.Cup("c3", {315, 495}, color, counts[2]));
    b.Pipe(f2, cups[1], {{190, 330}});
    b.Pipe(f2, cups[2], {{315, 330}});
    forks = 2;
  } else if (shape == 1) {
    const std::vector<int> counts = split(n, 4);
    const int f2 = b.Add(NodeKind::Fork, "f2", {95, 245}, true);
    const int f3 = b.Add(NodeKind::Fork, "f3", {265, 245}, true);
    b.Pipe(f1, f2, {{95, 170}});
    b.Pipe(f1, f3, {{265, 170}});
    const float xs[] = {45, 140, 220, 315};
    for (int i = 0; i < 4; ++i) {
      cups.push_back(b.Cup("c" + std::to_string(i + 1), {xs[i], 495}, color, counts[static_cast<size_t>(i)]));
      b.Pipe(i < 2 ? f2 : f3, cups.back(), {{xs[i], 330}});
    }
    forks = 3;
  } else {
    drained = rng.Range(1, 2);
    const std::vector<int> counts = split(n - drained, 2);
    const int f2 = b.Add(NodeKind::Fork, "f2", {250, 260}, true);
    cups.push_back(b.Cup("c1", {70, 495}, color, counts[0]));
    b.Pipe(f1, cups.back(), {{70, 180}});
    b.Pipe(f1, f2, {{250, 180}});
    cups.push_back(b.Cup("c2", {190, 495}, color, counts[1]));
    const int d = b.Add(NodeKind::Drain, "d", {320, 470});
    if (rng.Chance(0.5f)) {
      b.Pipe(f2, cups.back(), {{190, 340}});
      b.Pipe(f2, d, {{320, 340}});
    } else {
      b.Pipe(f2, d, {{320, 340}});
      b.Pipe(f2, cups.back(), {{190, 340}});
    }
    forks = 2;
  }
  // Flaps: sometimes tappable, set at random.
  for (Node& node : b.level.nodes) {
    if (node.kind != NodeKind::Fork) continue;
    node.flap = rng.Range(0, 1);
    node.flap_tappable = rng.Chance(difficulty <= 2 ? 0.4f : 0.25f);
  }
  const int pieces = std::min(forks, rng.Range(1, forks));
  for (int i = 0; i < pieces; ++i) {
    if (rng.Chance(0.5f)) {
      b.Tray(PieceType::Flipper);
    } else {
      const int cup = cups[static_cast<size_t>(rng.Range(0, static_cast<int>(cups.size()) - 1))];
      b.Tray(PieceType::Overflow, b.level.nodes[static_cast<size_t>(cup)].count);
    }
  }
  if (difficulty >= 3 && rng.Chance(0.5f)) {
    // One draw per statement: the order of a call's arguments isn't fixed in C++ (MSVC and clang differ),
    // and the same seed must make the same level on every device.
    const PieceType type = rng.Chance(0.5f) ? PieceType::Flipper : PieceType::Overflow;
    const int quota = rng.Range(1, 3);
    b.Tray(type, quota);
  }
  for (TrayPiece& piece : b.level.tray) {
    if (piece.piece.type == PieceType::Overflow && piece.piece.n < 1) piece.piece.n = 1;
  }
  return b.level;
}

// A mixer and partners that don't match up (World 4: joins).
Level Mixing(Rng& rng, int difficulty) {
  Builder b;
  const int pairs = rng.Range(3, 5);
  const int extra = difficulty >= 2 ? rng.Range(1, 2) : rng.Range(0, 1);
  const int beat = rng.Pick({10, 12});
  const int sb = b.Spout("sb", {80, 40}, Repeat(Bead::Pure(Color::Blue), pairs + extra), beat);
  const int sa = b.Spout("sa", {280, 40}, Repeat(Bead::Pure(Color::Amber), pairs), beat, rng.Range(1, 6));
  const int m = b.Add(NodeKind::Merge, "m", {180, 320}, true);
  if (extra > 0) {
    const int f = b.Add(NodeKind::Fork, "f", {80, 170}, true);
    b.Pipe(sb, f);
    b.Pipe(f, m, {{80, 260}});
    if (rng.Chance(0.5f)) {
      const int d = b.Add(NodeKind::Drain, "d", {30, 330});
      b.Pipe(f, d, {{30, 220}});
    } else {
      const int cb = b.Cup("cb", {40, 500}, Color::Blue, extra);
      b.Pipe(f, cb, {{30, 230}, {30, 420}, {40, 440}});
    }
  } else {
    b.Pipe(sb, m, {{80, 240}});
  }
  b.Pipe(sa, m, {{280, static_cast<float>(rng.Range(180, 260))}});
  const int cg = b.Cup("cg", {180, 500}, Color::Green, 2 * pairs);
  b.Pipe(m, cg);
  b.Tray(PieceType::Mixer);
  if (extra > 0) b.Tray(PieceType::Overflow, pairs);
  if (difficulty >= 2) b.Tray(PieceType::Turnstile);
  if (difficulty >= 3 && extra > 0) b.Tray(PieceType::Flipper);
  return b.level;
}

// Two colours take turns in one pipe (World 3: multiplexing a shared channel).
Level Shared(Rng& rng, int difficulty) {
  Builder b;
  const int n = rng.Range(3, 5);
  const int beat = rng.Pick({10, 12, 14, 16});
  const int sb = b.Spout("sb", {80, 40}, Repeat(Bead::Pure(Color::Blue), n), beat);
  const int sa = b.Spout("sa", {280, 40}, Repeat(Bead::Pure(Color::Amber), n), beat);
  const int pb = b.Add(NodeKind::Pass, "pb", {80, 120}, true);
  const int pa = b.Add(NodeKind::Pass, "pa", {280, 120}, true);
  const bool merge_socket = rng.Chance(difficulty >= 3 ? 0.4f : 0.6f);
  const int m = b.Add(NodeKind::Merge, "m", {180, 235}, merge_socket);
  const int f = b.Add(NodeKind::Fork, "f", {180, 430}, true);
  b.Pipe(sb, pb);
  b.Pipe(sa, pa);
  b.Pipe(pb, m, {{80, static_cast<float>(rng.Range(170, 190))}});
  b.Pipe(pa, m, {{280, static_cast<float>(rng.Range(170, 190))}});
  b.Pipe(m, f,
         {{180, 275},
          {static_cast<float>(rng.Range(100, 140)), 315},
          {static_cast<float>(rng.Range(220, 260)), 355},
          {180, 395}});
  const bool blue_left = rng.Chance(0.5f);
  const int cl = b.Cup("cl", {90, 515}, blue_left ? Color::Blue : Color::Amber, n);
  const int cr = b.Cup("cr", {270, 515}, blue_left ? Color::Amber : Color::Blue, n);
  b.Pipe(f, cl, {{90, 470}});
  b.Pipe(f, cr, {{270, 470}});
  b.Tray(PieceType::Separator);
  b.Tray(PieceType::TankShort);
  if (rng.Chance(0.6f)) b.Tray(PieceType::TankTall);
  if (merge_socket) b.Tray(PieceType::Turnstile);
  return b.level;
}

const char* kAdjectives[] = {"Copper",  "Glass",  "Velvet",   "Quiet",  "Silver",  "Hollow", "Bright", "Twin",
                             "Winding", "Little", "Northern", "Sleepy", "Crooked", "Golden", "Hidden", "Lucky"};
const char* kNouns[] = {"Weir", "Spring", "Loom",   "Cascade", "Channel", "Sluice", "Fountain", "Junction",
                        "Mill", "Delta",  "Siphon", "Valve",   "Canal",   "Brook",  "Press",    "Still"};

std::string NameFor(uint64_t seed) {
  Rng rng(seed ^ 0xA5A5A5A5ull);
  const int adjective = rng.Range(0, 15);  // one draw per statement (see Shares)
  const int noun = rng.Range(0, 15);
  return std::string(kAdjectives[adjective]) + " " + kNouns[noun];
}

uint64_t Mix(uint64_t x) {
  Rng rng(x);
  return rng.Next();
}

}  // namespace

Level Generate(uint64_t seed, int difficulty, int only_kind) {
  difficulty = std::clamp(difficulty, 0, 6);
  // How hard: pieces needed, the share of placements that solve, and how big the search is.
  static constexpr int kMinPieces[] = {1, 1, 2, 2, 2, 3, 3};
  static constexpr float kMaxRatio[] = {0.5f, 0.35f, 0.25f, 0.15f, 0.1f, 0.07f, 0.05f};
  static constexpr int kMinPlacements[] = {3, 4, 8, 12, 20, 30, 40};
  Rng rng(seed);
  Level best;
  float best_score = -1.0f;
  for (int attempt = 0; attempt < 120; ++attempt) {
    int kind = 0;
    if (difficulty <= 1)
      kind = rng.Pick({0, 0, 1});
    else if (difficulty <= 3)
      kind = rng.Pick({0, 0, 1, 3});
    else
      kind = rng.Pick({0, 0, 1, 2, 3});
    if (only_kind >= 0) kind = only_kind;
    Level level;
    switch (kind) {
      case 0: level = Braid(rng, difficulty); break;
      case 1: level = Shares(rng, difficulty); break;
      case 2: level = Mixing(rng, difficulty); break;
      default: level = Shared(rng, difficulty); break;
    }
    level.id = "generated";
    if (!level.Finalize().empty() || level.tray.empty()) continue;
    const SolveReport report = Solve(level, {.max_placements = 4000});
    if (report.solutions == 0) continue;
    Board empty(level);
    if (Simulate(level, empty.GetPlacement(), true).solved) continue;
    const float ratio = static_cast<float>(report.solutions) / static_cast<float>(std::max(1, report.placements));
    const bool good = report.pars.pieces >= kMinPieces[difficulty] && ratio <= kMaxRatio[difficulty] &&
                      report.placements >= kMinPlacements[difficulty];
    const float score = static_cast<float>(std::min(report.pars.pieces, kMinPieces[difficulty])) +
                        std::min(1.0f, kMaxRatio[difficulty] / std::max(ratio, 1e-3f));
    if (good || score > best_score) {
      level.pars = report.pars;
      best = level;
      best_score = score;
      if (good) break;
    }
  }
  return best;
}

Level GenerateLab(int64_t seed, const std::string& difficulty) {
  const int level_difficulty = difficulty == "easy" ? 1 : difficulty == "hard" ? 5 : 3;
  const uint64_t mixed =
      Mix(0x1AB0000000ull + static_cast<uint64_t>(seed) * 7919u + static_cast<uint64_t>(level_difficulty));
  Level level = Generate(mixed, level_difficulty);
  level.id = "lab-" + std::to_string(seed);
  level.name = NameFor(mixed);
  level.type = LevelType::Lab;
  return level;
}

}  // namespace threadlock
