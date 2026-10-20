// threadlock_tool: the level designer's command line (PC only).
//
//   threadlock_tool check  [assets]            every level: valid? solvable? pars as in pars.json?
//   threadlock_tool bake   [assets]            solves every level and writes assets/levels/pars.json
//   threadlock_tool solve  <id> [assets]       every solution, and how the small placements fail
//   threadlock_tool timing <id> [placement]    when each drop enters each junction (placement: none|lean|swift)
//   threadlock_tool render <id> <out.png> [none|lean|swift] [tick]   the board, and the drops at `tick`
//   threadlock_tool daily <yyyymmdd> <out.png>          generate a daily puzzle, solve it, render it
//   threadlock_tool lab <seed> <easy|medium|hard> <out.png>
//   threadlock_tool sounds <dir>                        every sound as a WAV + 30 s of ambience + a demo run
//
// `assets` defaults to ./assets (run it from the repo's root).
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#include "../src/ambience.h"
#include "../src/audio_synth.h"
#include "../src/generator.h"
#include "../src/level_io.h"
#include "../src/solver.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

using namespace threadlock;

namespace {

std::string ReadText(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  std::stringstream text;
  text << file.rdbuf();
  return text.str();
}

// False if a world file has problems (printed): its broken levels can't be run, so the callers stop. A
// pars file with problems is reported, and `pars_ok` says so.
bool LoadWorlds(const std::string& assets, std::vector<World>& worlds, bool with_pars, bool* pars_ok = nullptr) {
  bool ok = true;
  for (int n = 1; n <= 8; ++n) {
    const std::string path = assets + "/levels/world" + std::to_string(n) + ".json";
    const std::string text = ReadText(path);
    if (text.empty()) continue;
    World world;
    std::vector<std::string> errors;
    if (!ParseWorld(text, world, errors)) ok = false;
    for (const std::string& error : errors) std::printf("  %s: %s\n", path.c_str(), error.c_str());
    worlds.push_back(std::move(world));
  }
  if (!ok) {
    std::printf("fix the level files first\n");
    return false;
  }
  if (with_pars) {
    const std::string pars = ReadText(assets + "/levels/pars.json");
    std::vector<std::string> errors;
    const bool applied = pars.empty() || ApplyPars(pars, worlds, errors);
    if (pars_ok) *pars_ok = applied;
    for (const std::string& error : errors) std::printf("  pars.json: %s\n", error.c_str());
  }
  return true;
}

const Level* Find(const std::vector<World>& worlds, const std::string& id) {
  for (const World& world : worlds) {
    for (const Level& level : world.levels) {
      if (level.id == id) return &level;
    }
  }
  return nullptr;
}

Placement EmptyPlacement(const Level& level) {
  Board board(level);
  return board.GetPlacement();
}

Placement Pick(const Level& level, const std::string& which) {
  if (which == "lean" && level.pars.Known()) return level.pars.lean;
  if (which == "swift" && level.pars.Known()) return level.pars.swift;
  if (which == "lean" || which == "swift") {
    const SolveReport report = Solve(level);
    return which == "lean" ? report.pars.lean : report.pars.swift;
  }
  return EmptyPlacement(level);
}

std::string Seconds(int ticks) {
  char text[16];
  const int tenths = (ticks * 10 + 4) / 8;
  std::snprintf(text, sizeof(text), "%d:%02d", tenths / 600, tenths / 10 % 60);
  return text;
}

// ---- check / bake ----

int Check(const std::string& assets, bool bake) {
  std::vector<World> worlds;
  bool ok = true;
  if (!LoadWorlds(assets, worlds, !bake, &ok)) return 1;
  int levels = 0, problems = 0;
  std::vector<const Level*> all;
  const auto start = std::chrono::steady_clock::now();
  for (World& world : worlds) {
    std::printf("World %d: %s (%zu levels)\n", world.number, world.name.c_str(), world.levels.size());
    for (Level& level : world.levels) {
      ++levels;
      const auto t0 = std::chrono::steady_clock::now();
      const SolveReport report = Solve(level);
      const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
      const Outcome empty = Simulate(level, EmptyPlacement(level), false);
      std::string notes;
      if (report.solutions == 0) notes += " UNSOLVABLE";
      const bool has_movable =
          std::any_of(level.tray.begin(), level.tray.end(), [](const TrayPiece& piece) { return !piece.locked; });
      if (empty.solved && has_movable && level.type != LevelType::Spectacle) notes += " SOLVED-WITH-NOTHING";
      if (!bake && report.solutions > 0) {
        if (level.pars.pieces != report.pars.pieces || level.pars.ticks != report.pars.ticks) {
          notes +=
              " PARS-STALE (file " + std::to_string(level.pars.pieces) + "/" + std::to_string(level.pars.ticks) + ")";
        }
      }
      for (const std::string& problem : LookProblems(level)) notes += " LOOK(" + problem + ")";
      if (report.two_answers) notes += " two-answers";
      if (!notes.empty() && notes.find_first_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ") != std::string::npos) ++problems;
      std::printf("  %-6s %-22s %3d pieces / %-4s (%3d ticks) | %4d solutions of %6d | empty: %-9s | %6.1f ms%s\n",
                  level.id.c_str(), level.name.c_str(), report.pars.pieces, Seconds(report.pars.ticks).c_str(),
                  report.pars.ticks, report.solutions, report.placements, FailureKey(empty.failure), ms, notes.c_str());
      if (bake) {
        level.pars = report.pars;
        all.push_back(&level);
      }
    }
  }
  const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  std::printf("%d levels, %d with problems, %.1f s\n", levels, problems, seconds);
  if (bake) {
    std::ofstream out(assets + "/levels/pars.json", std::ios::binary);
    out << WritePars(all);
    std::printf("wrote %s/levels/pars.json\n", assets.c_str());
  }
  return ok && problems == 0 ? 0 : 1;
}

// ---- solve ----

int SolveOne(const Level& level) {
  const SolveReport report = Solve(level, {.keep = true});
  std::printf("%s %s: %d solutions of %d placements\n", level.id.c_str(), level.name.c_str(), report.solutions,
              report.placements);
  for (size_t i = 0; i < level.pipes.size(); ++i) {
    const Pipe& pipe = level.pipes[i];
    std::printf("  pipe %zu %s -> %s: %d cells (%.0f units)\n", i,
                level.nodes[static_cast<size_t>(pipe.from)].id.c_str(),
                level.nodes[static_cast<size_t>(pipe.to)].id.c_str(), pipe.cells, pipe.length);
  }
  std::vector<Solution> kept = report.kept;
  std::sort(kept.begin(), kept.end(), [](const Solution& a, const Solution& b) {
    return std::tie(a.pieces, a.ticks) < std::tie(b.pieces, b.ticks);
  });
  for (size_t i = 0; i < kept.size() && i < 40; ++i) {
    std::printf("  SOLVES  %d pieces, %3d ticks: %s\n", kept[i].pieces, kept[i].ticks,
                PlacementText(level, kept[i].placement).c_str());
  }
  if (report.solutions > 0) {
    std::printf("  pars: Lean %d pieces, Swift %d ticks (%s)%s\n", report.pars.pieces, report.pars.ticks,
                Seconds(report.pars.ticks).c_str(), report.two_answers ? ", two answers" : "");
  }
  // How placements with at most par + 1 pieces fail: the traps a player meets first.
  std::map<std::string, int> fails;
  const int limit = report.solutions > 0 ? report.pars.pieces + 1 : 99;
  int shown = 0;
  ForEachPlacement(level, [&](const Placement& placement) {
    int pieces = 0;
    for (size_t i = 0; i < placement.at.size(); ++i) pieces += placement.at[i] >= 0 && !level.tray[i].locked;
    if (pieces > limit) return true;
    const Outcome outcome = Simulate(level, placement, true);
    if (outcome.solved) return true;
    const std::string where =
        outcome.fail_node >= 0 ? level.nodes[static_cast<size_t>(outcome.fail_node)].id : std::string("?");
    ++fails[std::string(FailureKey(outcome.failure)) + "@" + where];
    if (shown++ < 30) {
      std::printf("  fails   %-9s at %-4s tick %3d: %s\n", FailureKey(outcome.failure), where.c_str(),
                  outcome.fail_tick, PlacementText(level, placement).c_str());
    }
    return true;
  });
  for (const auto& [what, count] : fails) std::printf("  %4d x %s\n", count, what.c_str());
  return 0;
}

// ---- timing ----

int Timing(const Level& level, const Placement& placement) {
  Board board(level);
  board.SetPlacement(placement);
  board.Start();
  std::printf("%s with %s\n", level.id.c_str(), PlacementText(level, placement).c_str());
  while (board.Running()) {
    for (const Event& e : board.Tick()) {
      const char* node = e.node >= 0 ? level.nodes[static_cast<size_t>(e.node)].id.c_str() : "";
      switch (e.type) {
        case EventType::Pour:
          std::printf("  %4d pour   %-4s %s\n", board.Ticks(), node, BeadText(e.bead).c_str());
          break;
        case EventType::Enter:
          if (level.nodes[static_cast<size_t>(e.node)].dwell > 0) {
            std::printf("  %4d enter  %-4s %-4s #%d\n", board.Ticks(), node, BeadText(e.bead).c_str(), e.drop);
          }
          break;
        case EventType::Wait: std::printf("  %4d wait   %-4s #%d\n", board.Ticks(), node, e.drop); break;
        case EventType::Collide: std::printf("  %4d MUD    %-4s\n", board.Ticks(), node); break;
        case EventType::NearMiss: std::printf("  %4d near   %-4s\n", board.Ticks(), node); break;
        case EventType::Mix:
          std::printf("  %4d mix    %-4s -> %s\n", board.Ticks(), node, BeadText(e.bead).c_str());
          break;
        case EventType::Land:
          if (e.slot >= 0) {
            std::printf("  %4d land   %-4s %-4s bottle %d count %d\n", board.Ticks(), node, BeadText(e.bead).c_str(),
                        e.slot, e.value);
          } else {
            std::printf("  %4d land   %-4s %-4s count %d\n", board.Ticks(), node, BeadText(e.bead).c_str(), e.value);
          }
          break;
        case EventType::Spill: std::printf("  %4d SPILL  %-4s\n", board.Ticks(), node); break;
        case EventType::Flip:
          std::printf("  %4d gate   %-4s %s\n", board.Ticks(), node, e.value ? "opens" : "shuts");
          break;
        case EventType::Turn: std::printf("  %4d turn   %-4s bottle %d\n", board.Ticks(), node, e.slot); break;
        case EventType::Jammed: std::printf("  %4d THREADLOCKED\n", board.Ticks()); break;
        default: break;
      }
    }
  }
  for (int piece = 0; piece < board.PieceCount(); ++piece) {
    if (board.PieceNode(piece) >= 0 && board.RoleOf(piece) != PieceRole::Acts) {
      std::printf("  %s on %s: %s\n", PieceKey(board.PieceInfo(piece).piece.type),
                  level.nodes[static_cast<size_t>(board.PieceNode(piece))].id.c_str(),
                  PieceRoleKey(board.RoleOf(piece)));
    }
  }
  const Outcome& outcome = board.GetOutcome();
  std::printf("%s after %d ticks (%s): %s\n", outcome.solved ? "SOLVED" : "failed", outcome.ticks,
              Seconds(outcome.ticks).c_str(), FailureKey(outcome.failure));
  return 0;
}

// ---- render ----

struct Image {
  int w, h;
  float scale;
  std::vector<uint8_t> rgb;
  stbtt_bakedchar glyphs[96];
  std::vector<uint8_t> atlas;
  bool font = false;

  Image(int width, int height, float s) : w(width), h(height), scale(s), rgb(static_cast<size_t>(width * height * 3)) {}

  void Blend(int x, int y, const float c[3], float a) {
    if (x < 0 || y < 0 || x >= w || y >= h || a <= 0.0f) return;
    uint8_t* p = &rgb[static_cast<size_t>((y * w + x) * 3)];
    for (int i = 0; i < 3; ++i) p[i] = static_cast<uint8_t>(p[i] + (c[i] * 255.0f - p[i]) * std::min(a, 1.0f));
  }
  // Distance-based shapes in board units (anti-aliased by one pixel).
  void Segment(Vec2 a, Vec2 b, float thickness, const float c[3]) {
    const float r = thickness * 0.5f * scale;
    const float ax = a.x * scale, ay = a.y * scale, bx = b.x * scale, by = b.y * scale;
    const int x0 = static_cast<int>(std::min(ax, bx) - r - 2), x1 = static_cast<int>(std::max(ax, bx) + r + 2);
    const int y0 = static_cast<int>(std::min(ay, by) - r - 2), y1 = static_cast<int>(std::max(ay, by) + r + 2);
    const float dx = bx - ax, dy = by - ay, len2 = std::max(dx * dx + dy * dy, 1e-6f);
    for (int y = y0; y <= y1; ++y) {
      for (int x = x0; x <= x1; ++x) {
        const float px = static_cast<float>(x) + 0.5f - ax, py = static_cast<float>(y) + 0.5f - ay;
        const float t = std::clamp((px * dx + py * dy) / len2, 0.0f, 1.0f);
        const float d = std::hypot(px - t * dx, py - t * dy);
        Blend(x, y, c, r - d + 0.5f);
      }
    }
  }
  void Polyline(const std::vector<Vec2>& points, float thickness, const float c[3]) {
    for (size_t i = 1; i < points.size(); ++i) Segment(points[i - 1], points[i], thickness, c);
  }
  void Disc(Vec2 center, float radius, const float c[3]) { Segment(center, center, radius * 2.0f, c); }
  void Ring(Vec2 center, float radius, float thickness, const float c[3]) {
    const float cx = center.x * scale, cy = center.y * scale, r = radius * scale, t = thickness * 0.5f * scale;
    for (int y = static_cast<int>(cy - r - t - 2); y <= static_cast<int>(cy + r + t + 2); ++y) {
      for (int x = static_cast<int>(cx - r - t - 2); x <= static_cast<int>(cx + r + t + 2); ++x) {
        const float d = std::fabs(std::hypot(static_cast<float>(x) + 0.5f - cx, static_cast<float>(y) + 0.5f - cy) - r);
        Blend(x, y, c, t - d + 0.5f);
      }
    }
  }
  void LoadFont(const std::string& path) {
    static std::string data;
    data = ReadText(path);
    if (data.empty()) return;
    atlas.assign(512 * 512, 0);
    font = stbtt_BakeFontBitmap(reinterpret_cast<const unsigned char*>(data.data()), 0, 13.0f * scale, atlas.data(),
                                512, 512, 32, 96, glyphs) > 0;
  }
  void Text(Vec2 at, const std::string& text, const float c[3]) {
    if (!font) return;
    float x = at.x * scale, y = at.y * scale;
    for (char ch : text) {
      if (ch < 32 || ch >= 128) continue;
      stbtt_aligned_quad q;
      stbtt_GetBakedQuad(glyphs, 512, 512, ch - 32, &x, &y, &q, 1);
      for (int py = static_cast<int>(q.y0); py < static_cast<int>(q.y1); ++py) {
        for (int px = static_cast<int>(q.x0); px < static_cast<int>(q.x1); ++px) {
          const int u = static_cast<int>(q.s0 * 512) + (px - static_cast<int>(q.x0));
          const int v = static_cast<int>(q.t0 * 512) + (py - static_cast<int>(q.y0));
          if (u < 0 || v < 0 || u >= 512 || v >= 512) continue;
          Blend(px, py, c, atlas[static_cast<size_t>(v * 512 + u)] / 255.0f);
        }
      }
    }
  }
};

const float* LiquidRgb(Color color) {
  static const float kRgb[][3] = {{0.5f, 0.5f, 0.5f},    {0.18f, 0.48f, 1.0f}, {1.0f, 0.77f, 0.24f},
                                  {0.25f, 0.88f, 0.63f}, {1.0f, 0.31f, 0.64f}, {0.95f, 0.96f, 1.0f},
                                  {0.43f, 0.35f, 0.6f},  {0.55f, 0.36f, 0.2f}};
  return kRgb[static_cast<size_t>(color)];
}

int Render(const Level& level, const std::string& out, const Placement& placement, int tick,
           const std::string& assets) {
  const float scale = 2.0f;
  const float height = level.look.height;
  Image image(static_cast<int>(360 * scale), static_cast<int>(height * scale), scale);
  image.LoadFont(assets + "/fonts/LilitaOne-Regular.ttf");
  const float bg[3] = {0.027f, 0.043f, 0.063f}, edge[3] = {0.075f, 0.13f, 0.18f}, inside[3] = {0.04f, 0.07f, 0.1f};
  const float ink[3] = {0.83f, 0.89f, 0.95f}, dim[3] = {0.4f, 0.46f, 0.52f}, socket[3] = {0.55f, 0.62f, 0.7f};
  for (size_t i = 0; i < image.rgb.size(); i += 3) {
    for (int k = 0; k < 3; ++k) image.rgb[i + static_cast<size_t>(k)] = static_cast<uint8_t>(bg[k] * 255.0f);
  }
  // A faint 20-unit grid to read coordinates off.
  for (int g = 20; g < static_cast<int>(height); g += 20)
    image.Segment({0, static_cast<float>(g)}, {360, static_cast<float>(g)}, 0.4f, edge);
  for (int g = 20; g < 360; g += 20)
    image.Segment({static_cast<float>(g), 0}, {static_cast<float>(g), height}, 0.4f, edge);
  for (const Pipe& pipe : level.pipes) image.Polyline(pipe.points, 24.0f, edge);
  for (const Pipe& pipe : level.pipes) image.Polyline(pipe.points, 17.0f, inside);
  Board board(level);
  board.SetPlacement(placement);
  if (tick > 0) {
    board.Start();
    while (board.Running() && board.Ticks() < tick) board.Tick();
  }
  for (size_t n = 0; n < level.nodes.size(); ++n) {
    const Node& node = level.nodes[n];
    switch (node.kind) {
      case NodeKind::Spout:
        image.Disc(node.at, 22.0f, inside);
        image.Ring(node.at, 22.0f, 2.5f, dim);
        if (!node.beads.empty()) image.Disc(node.at, 9.0f, LiquidRgb(node.beads[0].a));
        if (!node.beads.empty() && node.beads[0].IsBlend()) image.Disc(node.at, 5.0f, LiquidRgb(node.beads[0].b));
        break;
      case NodeKind::Cross:
      case NodeKind::Merge:
        image.Disc(node.at, 15.0f, inside);
        image.Ring(node.at, 15.0f, 2.0f, node.socket ? socket : edge);
        break;
      case NodeKind::Fork:
      case NodeKind::Pass: image.Ring(node.at, node.socket ? 11.0f : 6.0f, 2.0f, node.socket ? socket : dim); break;
      case NodeKind::Cup: {
        const float* c = LiquidRgb(node.want);
        image.Polyline({{node.at.x - 26, node.at.y},
                        {node.at.x - 26, node.at.y + 60},
                        {node.at.x + 26, node.at.y + 60},
                        {node.at.x + 26, node.at.y}},
                       3.0f, c);
        image.Text({node.at.x - 8, node.at.y + 40}, std::to_string(node.count), ink);
        if (!node.district.empty())  // World 3: the district this harbour serves, under it
          image.Text({node.at.x - 3.3f * static_cast<float>(node.district.size()), node.at.y + 74}, node.district, dim);
        break;
      }
      case NodeKind::Drain:
        image.Disc(node.at, 14.0f, inside);
        image.Ring(node.at, 14.0f, 2.5f, dim);
        break;
      case NodeKind::Rack: {
        // The bottles in a row under the tap (`at`); the one under it now is ringed.
        const int k = static_cast<int>(node.slots.size());
        const int under = board.Started() ? board.RackSlot(static_cast<int>(n)) : node.first;
        for (int s = 0; s < k; ++s) {
          const Vec2 c{node.at.x + (static_cast<float>(s - under)) * 34.0f, node.at.y + 30.0f};
          const Slot& slot = node.slots[static_cast<size_t>(s)];
          image.Polyline({{c.x - 13, c.y - 18}, {c.x - 13, c.y + 18}, {c.x + 13, c.y + 18}, {c.x + 13, c.y - 18}}, 2.5f,
                         LiquidRgb(slot.want));
          const int have = board.Started() ? board.Bottle(static_cast<int>(n), s).count : 0;
          image.Text({c.x - 10, c.y + 6}, std::to_string(have) + "/" + std::to_string(slot.count), ink);
          if (s == under) image.Ring(c, 24.0f, 1.5f, socket);
        }
        image.Text({node.at.x - 30, node.at.y + 66}, "turns /" + std::to_string(node.period), dim);
        break;
      }
      case NodeKind::Mill: {
        image.Disc(node.at, 15.0f, inside);
        image.Ring(node.at, 15.0f, 2.5f, ink);
        const float turn = static_cast<float>(board.MillTurns(static_cast<int>(n))) * 0.5f;
        for (int spoke = 0; spoke < 4; ++spoke) {
          const float a = turn + static_cast<float>(spoke) * 1.5708f;
          image.Segment(node.at, {node.at.x + std::cos(a) * 13.0f, node.at.y + std::sin(a) * 13.0f}, 2.0f, ink);
        }
        for (size_t g = 0; g < node.gears.size(); ++g) {  // a dashed rod to each gate it flips, through its bends
          const std::vector<Vec2> rod = RodPoints(level, static_cast<int>(n), static_cast<int>(g));
          for (size_t r = 0; r + 1 < rod.size(); ++r) {
            const Vec2 from = rod[r], to = rod[r + 1];
            const int dashes = std::max(2, static_cast<int>(std::hypot(to.x - from.x, to.y - from.y) / 12.0f)) * 2;
            for (int k = 0; k < dashes; k += 2) {
              const float t0 = static_cast<float>(k) / static_cast<float>(dashes);
              const float t1 = static_cast<float>(k + 1) / static_cast<float>(dashes);
              image.Segment({from.x + (to.x - from.x) * t0, from.y + (to.y - from.y) * t0},
                            {from.x + (to.x - from.x) * t1, from.y + (to.y - from.y) * t1}, 1.0f, dim);
            }
          }
        }
        break;
      }
      case NodeKind::Gate: {
        const bool open = board.GateOpen(static_cast<int>(n));
        static const float kOpen[3] = {0.35f, 0.85f, 0.45f}, kShut[3] = {1.0f, 0.35f, 0.29f};
        image.Segment({node.at.x - 13, node.at.y}, {node.at.x + 13, node.at.y}, open ? 2.0f : 5.0f,
                      open ? kOpen : kShut);
        image.Disc({node.at.x + 16, node.at.y - 8}, 3.5f, open ? kOpen : kShut);
        break;
      }
    }
    const int piece = board.PieceAt(static_cast<int>(n));
    if (piece >= 0) {
      const PieceType type = board.PieceInfo(piece).piece.type;
      const PieceRole role = board.RoleOf(piece);
      image.Ring(node.at, 13.0f, 3.0f, role == PieceRole::Acts ? ink : dim);
      std::string label = PieceKey(type);
      label = label.substr(0, 3);
      if (HasSetting(type)) label += "/" + std::to_string(board.Setting(piece));
      if (role != PieceRole::Acts) label += std::string(" ") + PieceRoleKey(role);
      image.Text({node.at.x - 12, node.at.y + 4}, label, role == PieceRole::Acts ? ink : dim);
    }
    image.Text({node.at.x + 14, node.at.y - 10}, node.id, socket);
  }
  // Cells per pipe, at its middle.
  for (const Pipe& pipe : level.pipes) {
    const Vec2 mid = PointAlong(pipe.points, pipe.length * 0.5f);
    image.Text({mid.x + 8, mid.y + 4}, std::to_string(pipe.cells), dim);
  }
  if (tick > 0) {
    for (const Drop& drop : board.Drops()) {
      if (!drop.alive) continue;
      const Vec2 p = DropPoint(level, drop, drop.loc);
      image.Disc(p, 5.0f, LiquidRgb(drop.bead.a));
      if (drop.bead.IsBlend()) image.Disc(p, 2.5f, LiquidRgb(drop.bead.b));
    }
    image.Text({8, height - 10}, "tick " + std::to_string(board.Ticks()), ink);
  }
  image.Text({8, 16}, level.id + "  " + level.name, ink);
  if (!stbi_write_png(out.c_str(), image.w, image.h, 3, image.rgb.data(), image.w * 3)) {
    std::printf("can't write %s\n", out.c_str());
    return 1;
  }
  std::printf("wrote %s\n", out.c_str());
  return 0;
}

// ---- sounds <dir>: every synthesized sound as a WAV, to listen to on the PC ----

bool WriteWav(const std::string& path, const std::vector<float>& stereo, uint32_t rate) {
  std::ofstream file(path, std::ios::binary);
  if (!file) return false;
  auto u32 = [&](uint32_t v) { file.write(reinterpret_cast<const char*>(&v), 4); };
  auto u16 = [&](uint16_t v) { file.write(reinterpret_cast<const char*>(&v), 2); };
  const uint32_t bytes = static_cast<uint32_t>(stereo.size() * 2);
  file.write("RIFF", 4);
  u32(36 + bytes);
  file.write("WAVEfmt ", 8);
  u32(16);
  u16(1);  // PCM
  u16(2);
  u32(rate);
  u32(rate * 4);
  u16(4);
  u16(16);
  file.write("data", 4);
  u32(bytes);
  for (float s : stereo) {
    u16(static_cast<uint16_t>(static_cast<int16_t>(std::lround(std::clamp(s, -1.0f, 1.0f) * 32767.0f))));
  }
  return static_cast<bool>(file);
}

// Adds a mono sound into a stereo track at `at` seconds, at `pitch` (linear interpolation, as the mixer).
void MixInto(std::vector<float>& stereo, const std::vector<float>& mono, uint32_t rate, float at, float volume,
             float pitch) {
  const size_t start = static_cast<size_t>(at * static_cast<float>(rate));
  for (size_t i = 0;; ++i) {
    const float pos = static_cast<float>(i) * pitch;
    const size_t k = static_cast<size_t>(pos);
    if (k + 1 >= mono.size() || 2 * (start + i) + 1 >= stereo.size()) break;
    const float s = volume * (mono[k] + (pos - static_cast<float>(k)) * (mono[k + 1] - mono[k]));
    stereo[2 * (start + i)] += s;
    stereo[2 * (start + i) + 1] += s;
  }
}

int Sounds(const std::string& dir) {
  constexpr uint32_t kRate = 48000;
  std::vector<std::vector<float>> made(static_cast<size_t>(synth::Sound::Count));
  const auto t0 = std::chrono::steady_clock::now();
  for (size_t i = 0; i < made.size(); ++i) made[i] = synth::Make(static_cast<synth::Sound>(i), kRate);
  const double synth_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  size_t frames = 0;
  for (size_t i = 0; i < made.size(); ++i) {
    std::vector<float> stereo(made[i].size() * 2);
    for (size_t k = 0; k < made[i].size(); ++k) stereo[2 * k] = stereo[2 * k + 1] = made[i][k];
    const std::string path = dir + "/" + synth::SoundName(static_cast<synth::Sound>(i)) + ".wav";
    if (!WriteWav(path, stereo, kRate)) {
      std::printf("can't write %s\n", path.c_str());
      return 1;
    }
    frames += made[i].size();
  }
  std::printf("%zu sounds, %.1f s of audio, synthesized in %.1f ms (%.1f MB as stereo float)\n", made.size(),
              static_cast<double>(frames) / kRate, synth_ms, static_cast<double>(frames) * 8.0 / 1048576.0);

  // 30 s of the ambience, rendered in the mixer's block size, fading in over 2 s.
  constexpr size_t kBedFrames = 30 * kRate;
  std::vector<float> bed(kBedFrames * 2);
  Ambience ambience(kRate);
  ambience.SetTarget(1.0f, 2.0f);
  const auto t1 = std::chrono::steady_clock::now();
  for (size_t at = 0; at < kBedFrames; at += 512) {
    ambience.Render(bed.data() + 2 * at, static_cast<uint32_t>(std::min<size_t>(512, kBedFrames - at)));
  }
  const double bed_us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t1).count();
  std::printf("ambience: %.0f us per second of audio\n", bed_us / 30.0);
  WriteWav(dir + "/Ambience.wav", bed, kRate);

  // A World 1 run in miniature over the bed (ducked to 60%), to judge the mix: a crucible's beats, blue and
  // amber drops climbing, panes chiming, a mud puff, drips, both lanterns filling, the heart.
  std::vector<float> run(bed.begin(), bed.begin() + 16 * kRate * 2);
  for (float& s : run) s *= 0.6f;
  auto play = [&](synth::Sound s, float t, float volume, float pitch = 1.0f) {
    MixInto(run, made[static_cast<size_t>(s)], kRate, t, volume, pitch);
  };
  for (int beat = 0; beat < 12; ++beat) play(synth::Sound::CruciblePour, 0.5f + 0.5f * static_cast<float>(beat), 0.22f);
  for (int k = 0; k < 6; ++k) {
    const float t = 1.0f + static_cast<float>(k);
    play(synth::Sound::HarmonicaNote, t, 0.55f, synth::ClimbPitch(k));
    play(synth::Sound::CelestaNote, t + 0.5f, 0.55f, synth::ClimbPitch(k));
    play(synth::Sound::PaneChime, t + 0.02f, 0.4f, synth::ClimbPitch(k));
    play(synth::Sound::Drip, t - 0.25f, 0.25f, 0.9f + 0.05f * static_cast<float>(k % 3));
  }
  play(synth::Sound::Smoke, 3.75f, 0.9f);
  play(synth::Sound::LanternBlue, 7.0f, 0.6f);
  play(synth::Sound::LanternAmber, 7.5f, 0.6f);
  play(synth::Sound::Heart, 9.5f, 0.9f);
  WriteWav(dir + "/demo_lantern_run.wav", run, kRate);

  // 30 s of the shop's bed (World 2), and 2-7 Morning Orders in miniature over it: the jar's beats, blue and
  // amber drops climbing, the lazy susan clacking every 4 beats, an order paid, sludge, a spill, the till.
  std::vector<float> shop_bed(kBedFrames * 2);
  Ambience shop(kRate);
  shop.SetBed(Ambience::Bed::Shop);
  shop.SetTarget(1.0f, 2.0f);
  for (size_t at = 0; at < kBedFrames; at += 512) {
    shop.Render(shop_bed.data() + 2 * at, static_cast<uint32_t>(std::min<size_t>(512, kBedFrames - at)));
  }
  WriteWav(dir + "/AmbienceShop.wav", shop_bed, kRate);
  run.assign(shop_bed.begin(), shop_bed.begin() + 16 * kRate * 2);
  for (float& s : run) s *= 0.6f;
  for (int beat = 0; beat < 12; ++beat) play(synth::Sound::JarTap, 0.5f + 0.5f * static_cast<float>(beat), 0.22f);
  for (int beat = 0; beat < 3; ++beat) play(synth::Sound::RackClack, 1.0f + 2.0f * static_cast<float>(beat), 0.3f);
  for (int k = 0; k < 4; ++k) {
    const float t = 1.0f + static_cast<float>(k);
    play(synth::Sound::MarimbaNote, t, 0.55f, synth::ClimbPitch(k));
    play(synth::Sound::PizzNote, t + 0.5f, 0.55f, synth::ClimbPitch(k));
  }
  play(synth::Sound::PaidBlue, 5.0f, 0.6f);
  play(synth::Sound::Sludge, 7.0f, 0.9f);
  play(synth::Sound::SpillWood, 8.0f, 0.75f);
  play(synth::Sound::PaidAmber, 9.5f, 0.6f);
  play(synth::Sound::Till, 9.5f, 0.7f);  // the last order and the solved run start together
  WriteWav(dir + "/demo_shop_run.wav", run, kRate);
  std::printf("wrote %s/*.wav (+ Ambience.wav, AmbienceShop.wav, demo_lantern_run.wav, demo_shop_run.wav)\n",
              dir.c_str());
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::printf("usage: threadlock_tool check|bake|solve|timing|render ... (see the file's header)\n");
    return 2;
  }
  const std::string command = argv[1];
  if (command == "sounds" && argc > 2) return Sounds(argv[2]);
  if (command == "check" || command == "bake") return Check(argc > 2 ? argv[2] : "assets", command == "bake");
  // format [assets]: rewrites every world file in one tidy layout (a node per line).
  if (command == "format") {
    const std::string assets = argc > 2 ? argv[2] : "assets";
    for (int n = 1; n <= 8; ++n) {
      const std::string path = assets + "/levels/world" + std::to_string(n) + ".json";
      const std::string text = ReadText(path);
      if (text.empty()) continue;
      World world;
      std::vector<std::string> errors;
      ParseWorld(text, world, errors);
      if (!errors.empty()) {
        std::printf("%s has problems, not formatted\n", path.c_str());
        continue;
      }
      std::string out = "{\n  \"world\": " + std::to_string(world.number) + ",\n  \"name\": \"" + world.name +
                        "\",\n  \"accent\": \"" + ColorLetter(world.accent) + "\",\n  \"levels\": [\n";
      for (size_t i = 0; i < world.levels.size(); ++i) {
        std::string level = WriteLevel(world.levels[i]);
        std::string indented = "    ";
        for (char c : level) {
          indented += c;
          if (c == '\n') indented += "    ";
        }
        out += indented + (i + 1 < world.levels.size() ? ",\n" : "\n");
      }
      out += "  ]\n}\n";
      std::ofstream(path, std::ios::binary) << out;
      std::printf("formatted %s (%zu levels)\n", path.c_str(), world.levels.size());
    }
    return 0;
  }
  if ((command == "daily" && argc > 3) || (command == "lab" && argc > 4)) {
    Level level = command == "daily" ? GenerateDaily(std::atoi(argv[2])) : GenerateLab(std::atoll(argv[2]), argv[3]);
    std::printf("%s \"%s\": %zu nodes, %zu pipes, tray %zu, pars %d pieces / %d ticks, %d solutions\n",
                level.id.c_str(), level.name.c_str(), level.nodes.size(), level.pipes.size(), level.tray.size(),
                level.pars.pieces, level.pars.ticks, level.pars.solutions);
    std::printf("  lean: %s\n  swift: %s\n", PlacementText(level, level.pars.lean).c_str(),
                PlacementText(level, level.pars.swift).c_str());
    return Render(level, argv[command == "daily" ? 3 : 4], level.pars.lean, 0, "assets");
  }
  // search <kind> <difficulty> <first seed> <count> <min pieces> <max solutions> [out dir]: generated
  // machines good enough to start a handmade level from, as level JSON (and PNGs when given a dir).
  if (command == "search" && argc > 7) {
    const int kind = std::atoi(argv[2]), difficulty = std::atoi(argv[3]);
    const uint64_t first = std::strtoull(argv[4], nullptr, 10);
    const int count = std::atoi(argv[5]), min_pieces = std::atoi(argv[6]), max_solutions = std::atoi(argv[7]);
    int found = 0;
    for (uint64_t seed = first; seed < first + 400 && found < count; ++seed) {
      Level level = Generate(seed, difficulty, kind);
      if (level.nodes.empty() || level.pars.pieces < min_pieces || level.pars.solutions > max_solutions) continue;
      ++found;
      level.id = "s" + std::to_string(seed);
      level.name = "seed " + std::to_string(seed);
      std::printf("// seed %llu: %d pieces, %d ticks, %d solutions; lean %s\n%s,\n",
                  static_cast<unsigned long long>(seed), level.pars.pieces, level.pars.ticks, level.pars.solutions,
                  PlacementText(level, level.pars.lean).c_str(), WriteLevel(level).c_str());
      if (argc > 8)
        Render(level, std::string(argv[8]) + "/s" + std::to_string(seed) + ".png", level.pars.lean, 0, "assets");
    }
    return 0;
  }
  if (argc < 3) return 2;
  const std::string id = argv[2];
  std::string assets = "assets";
  std::vector<World> worlds;
  if (!LoadWorlds(assets, worlds, true)) return 1;
  const Level* level = Find(worlds, id);
  if (!level) {
    std::printf("no level %s\n", id.c_str());
    return 1;
  }
  if (command == "solve") return SolveOne(*level);
  if (command == "timing") return Timing(*level, Pick(*level, argc > 3 ? argv[3] : "none"));
  if (command == "render" && argc > 3) {
    return Render(*level, argv[3], Pick(*level, argc > 4 ? argv[4] : "none"), argc > 5 ? std::atoi(argv[5]) : 0,
                  assets);
  }
  return 2;
}
