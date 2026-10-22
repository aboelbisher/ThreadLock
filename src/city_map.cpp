#include "city_map.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace threadlock::city {
namespace {

constexpr float kDegrees = std::numbers::pi_v<float> / 180.0f;
constexpr int kMainStars = kSolveStar | kLeanStar | kSwiftStar;

Vec2 Add(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
Vec2 Sub(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
Vec2 Scale(Vec2 a, float s) { return {a.x * s, a.y * s}; }

// The angle of a point around the window's centre, in degrees within 180 of `near`.
float AngleNear(Vec2 at, float near) {
  float a = std::atan2(at.y - kWindow.y, at.x - kWindow.x) / kDegrees;
  while (a - near > 180.0f) a -= 360.0f;
  while (a - near < -180.0f) a += 360.0f;
  return a;
}

// The big pane's pointed end: a curve from the shoulder on one side, out past the bulge, to the other.
Cubic TipCurve(int petal) {
  const float th = PetalAngle(petal), hw = kPetalHalf;
  return {Polar(kTipShoulder, th - hw), Polar(kTipBulge, th - hw * 0.75f), Polar(kTipBulge, th + hw * 0.75f),
          Polar(kTipShoulder, th + hw)};
}

Band Sector(float r0, float r1, float a0, float a1, int steps) {
  Band band;
  steps = std::max(1, steps);
  for (int k = 0; k <= steps; ++k) {
    const float a = a0 + (a1 - a0) * static_cast<float>(k) / static_cast<float>(steps);
    band.inner.push_back(Polar(r0, a));
    band.outer.push_back(Polar(r1, a));
  }
  return band;
}

// One Catmull-Rom piece of the curve through `points`, from points[j] to points[j + 1].
Cubic CatmullRom(const std::vector<Vec2>& points, int j) {
  const int last = static_cast<int>(points.size()) - 1;
  const Vec2 p0 = points[std::max(0, j - 1)], p1 = points[j], p2 = points[j + 1], p3 = points[std::min(last, j + 2)];
  return {p1, Add(p1, Scale(Sub(p2, p0), 1.0f / 6.0f)), Sub(p2, Scale(Sub(p3, p1), 1.0f / 6.0f)), p2};
}

std::vector<Vec2> MainPoints() {
  std::vector<Vec2> points;
  for (int i = 0; i < kPetals; ++i) points.push_back(Lanterns()[i].at);
  return points;
}

std::vector<Vec2> BranchPoints() {
  std::vector<Vec2> points{Lanterns()[kPetals - 1].at};
  for (int i = kPetals; i < kLanterns; ++i) points.push_back(Lanterns()[i].at);
  return points;
}

const WorldLevels* FindWorld(const MapLevels& levels, int world) {
  for (const WorldLevels& w : levels) {
    if (w.world == world) return &w;
  }
  return nullptr;
}

int BitsOf(const StarMap& stars, const std::string& id) {
  const auto it = stars.find(id);
  return it == stars.end() ? 0 : it->second;
}

bool Solved(const StarMap& stars, const std::string& id) { return BitsOf(stars, id) & kSolveStar; }

// The lights of a world's main path (`count` places) then its challenges (`challenges` places).
template <size_t N>
std::array<Light, N> WorldLights(const MapLevels& levels, const StarMap& stars, int world, int count) {
  std::array<Light, N> lights{};
  const WorldLevels* w = FindWorld(levels, world);
  if (!w || !GateOpen(levels, stars, world)) return lights;
  const int main = std::min(count, static_cast<int>(w->main.size()));
  for (int i = 0; i < main; ++i) {
    lights[i].bits = BitsOf(stars, w->main[i]);
    // A solved level never looks locked (progress from another device may skip one).
    lights[i].locked = i > 0 && !Solved(stars, w->main[i - 1]) && !(lights[i].bits & kSolveStar);
  }
  const bool finished = !w->main.empty() && Solved(stars, w->main.back());
  const int challenges = std::min(static_cast<int>(N) - count, static_cast<int>(w->challenges.size()));
  for (int i = 0; i < challenges; ++i) {
    lights[count + i].bits = BitsOf(stars, w->challenges[i]);
    lights[count + i].locked = !finished && !(lights[count + i].bits & kSolveStar);
  }
  return lights;
}

// A pipe drops straight down out of its top end and straight into its bottom end.
Cubic DownPipe(Vec2 from, Vec2 to) { return {from, {from.x, from.y + 110.0f}, {to.x, to.y - 120.0f}, to}; }

Cubic Straight(Vec2 from, Vec2 to) {
  return {from, Add(from, Scale(Sub(to, from), 1.0f / 3.0f)), Add(from, Scale(Sub(to, from), 2.0f / 3.0f)), to};
}

}  // namespace

// ---- The rose window ----

float PetalAngle(int petal) { return -90.0f + 24.0f * static_cast<float>(petal); }

Vec2 Polar(float radius, float degrees) {
  return {kWindow.x + radius * std::cos(degrees * kDegrees), kWindow.y + radius * std::sin(degrees * kDegrees)};
}

Band PetalTip(int petal, int steps) {
  const Cubic tip = TipCurve(petal);
  const float th = PetalAngle(petal);
  Band band;
  steps = std::max(1, steps);
  for (int k = 0; k <= steps; ++k) {
    const Vec2 out = tip.At(static_cast<float>(k) / static_cast<float>(steps));
    band.inner.push_back(Polar(kTipInner, AngleNear(out, th)));
    band.outer.push_back(out);
  }
  return band;
}

float TipOuter(int petal, float degrees) {
  // The curve's angle grows along it: find the point at `degrees` by bisection.
  const Cubic tip = TipCurve(petal);
  const float th = PetalAngle(petal);
  float lo = 0.0f, hi = 1.0f;
  for (int i = 0; i < 30; ++i) {
    const float mid = (lo + hi) * 0.5f;
    if (AngleNear(tip.At(mid), th) < degrees) {
      lo = mid;
    } else {
      hi = mid;
    }
  }
  const Vec2 at = tip.At((lo + hi) * 0.5f);
  return std::sqrt((at.x - kWindow.x) * (at.x - kWindow.x) + (at.y - kWindow.y) * (at.y - kWindow.y));
}

Band PetalSliver(int petal, int k, int n, int steps) {
  const float th = PetalAngle(petal), hw = kPetalHalf;
  n = std::max(1, n);
  const float a0 = th - hw + 2.0f * hw * static_cast<float>(k) / static_cast<float>(n);
  const float a1 = th - hw + 2.0f * hw * static_cast<float>(k + 1) / static_cast<float>(n);
  Band band;
  steps = std::max(1, steps);
  for (int s = 0; s <= steps; ++s) {
    const float a = a0 + (a1 - a0) * static_cast<float>(s) / static_cast<float>(steps);
    band.inner.push_back(Polar(kTipInner, a));
    band.outer.push_back(Polar(TipOuter(petal, a), a));
  }
  return band;
}

Band LeanPane(int petal, int steps) {
  const float th = PetalAngle(petal);
  return Sector(kSmallInner, kSmallOuter, th - kPetalHalf, th, steps);
}

Band SwiftPane(int petal, int steps) {
  const float th = PetalAngle(petal);
  return Sector(kSmallInner, kSmallOuter, th, th + kPetalHalf, steps);
}

Vec2 LobeCentre(int lobe) { return Polar(kLobeRing, -90.0f + 120.0f * static_cast<float>(lobe)); }

// ---- The lantern path ----

const std::array<Lantern, kLanterns>& Lanterns() {
  static const std::array<Lantern, kLanterns> lanterns = [] {
    std::array<Lantern, kLanterns> out{};
    for (int i = 0; i < kPetals; ++i) {
      Lantern& l = out[i];
      l.at = {195.0f + 115.0f * std::sin(static_cast<float>(i) * 0.85f), 714.0f + 84.0f * static_cast<float>(i)};
      l.stars_y = l.at.y + 42.0f;
    }
    const Vec2 diamonds[kLobes] = {{212.0f, 1952.0f}, {282.0f, 1982.0f}, {350.0f, 1948.0f}};
    for (int i = 0; i < kLobes; ++i) {
      Lantern& l = out[kPetals + i];
      l.at = diamonds[i];
      l.diamond = true;
      l.radius = 31.0f;
      l.ring = 40.0f;
      l.hit = 40.0f;
      l.stars_y = l.at.y + 46.0f;
    }
    return out;
  }();
  return lanterns;
}

int HitLantern(Vec2 at) {
  int best = -1;
  float best_d = 0.0f;
  for (int i = 0; i < kLanterns; ++i) {
    const Lantern& l = Lanterns()[i];
    const float dx = at.x - l.at.x, dy = at.y - l.at.y;
    const float d = l.diamond ? std::fabs(dx) + std::fabs(dy) : std::sqrt(dx * dx + dy * dy);
    if (d <= l.hit && (best < 0 || d < best_d)) {
      best = i;
      best_d = d;
    }
  }
  return best;
}

const std::vector<TrailPiece>& Trail() {
  static const std::vector<TrailPiece> trail = [] {
    std::vector<TrailPiece> out;
    for (int j = 0; j + 1 < kPetals; ++j) out.push_back({j, j + 1});
    out.push_back({kPetals - 1, kPetals});
    for (int j = kPetals; j + 1 < kLanterns; ++j) out.push_back({j, j + 1});
    return out;
  }();
  return trail;
}

std::vector<Vec2> TrailPoints(int piece, int samples) {
  // The main path is one smooth curve through its lanterns, the side branch another from 1-15.
  if (piece < kPetals - 1) return CatmullRom(MainPoints(), piece).Points(samples);
  return CatmullRom(BranchPoints(), piece - (kPetals - 1)).Points(samples);
}

// ---- Districts ----

const std::array<District, kDistricts>& Districts() {
  static const std::array<District, kDistricts> districts = {{
      {1, 0.0f, 2500.0f, "THE LANTERN WORKS", 0.0f, 262.0f, 0.0f, 0.0f, 0.0f},
      {2, 2500.0f, 3764.0f, "THE MARKET SQUARE", 2412.0f, 2452.0f, 3062.0f, 3442.0f, 0.0f},
      {3, 3764.0f, 5008.0f, "THE CANAL QUARTER", 3724.0f, 3760.0f, 3994.0f, 4634.0f, 0.0f},
      {4, 5008.0f, kHeight, "THE SANCTUM", 4978.0f, 5128.0f, 5238.0f, 5888.0f, 2.6f},
  }};
  return districts;
}

int DistrictAt(float y) {
  for (const District& d : Districts()) {
    if (y < d.bottom) return d.world;
  }
  return Districts().back().world;
}

std::array<Vec2, kDistrictLamps> DistrictLamps(int world) {
  std::array<Vec2, kDistrictLamps> lamps{};
  if (world < 2 || world > kDistricts) return lamps;
  if (world == 2) {
    constexpr float kRows[3] = {3062.0f, 3192.0f, 3322.0f}, kColumns[5] = {60.0f, 128.0f, 195.0f, 262.0f, 330.0f};
    for (int j = 0; j < 15; ++j) {
      const int row = j / 5, column = j % 5;
      lamps[j] = {kColumns[row == 1 ? 4 - column : column], kRows[row]};
    }
    for (int k = 0; k < kLobes; ++k) lamps[15 + k] = {kColumns[3 - k], 3442.0f};
    return lamps;
  }
  if (world == 3) {
    constexpr float kRows[5] = {3994.0f, 4154.0f, 4314.0f, 4474.0f, 4634.0f}, kColumns[3] = {75.0f, 195.0f, 315.0f};
    for (int j = 0; j < 15; ++j) {
      const int row = j / 3, column = j % 3;
      lamps[j] = {kColumns[row % 2 ? 2 - column : column], kRows[row]};
    }
    for (int k = 0; k < kLobes; ++k) lamps[15 + k] = {kColumns[2 - k], 4788.0f};
    return lamps;
  }
  const District& d = Districts()[world - 1];
  for (int j = 0; j < kDistrictLamps; ++j) {
    const float f = static_cast<float>(j);
    lamps[j] = {195.0f + 120.0f * std::sin(f * 0.75f + d.phase),
                d.lamps_top + (d.lamps_bottom - d.lamps_top) * f / static_cast<float>(kDistrictLamps - 1)};
  }
  return lamps;
}

int HitLamp(int world, Vec2 at) {
  if (world < 2 || world > kDistricts) return -1;
  const auto lamps = DistrictLamps(world);
  int best = -1;
  float best_d = kLampHit;
  for (int j = 0; j < kDistrictLamps; ++j) {
    const float dx = at.x - lamps[j].x, dy = at.y - lamps[j].y;
    const float d = std::sqrt(dx * dx + dy * dy);
    if (d <= best_d) best = j, best_d = d;
  }
  return best;
}

Vec2 Cubic::At(float t) const {
  const float u = 1.0f - t;
  const float wa = u * u * u, wb = 3.0f * u * u * t, wc = 3.0f * u * t * t, wd = t * t * t;
  return {wa * a.x + wb * b.x + wc * c.x + wd * d.x, wa * a.y + wb * b.y + wc * c.y + wd * d.y};
}

std::vector<Vec2> Cubic::Points(int samples) const {
  samples = std::max(1, samples);
  std::vector<Vec2> points;
  for (int k = 0; k <= samples; ++k) points.push_back(At(static_cast<float>(k) / static_cast<float>(samples)));
  return points;
}

Cubic GatePipe(int world) {
  if (world < 2 || world > kDistricts) return {};
  const District& d = Districts()[world - 1];
  if (world <= 3) return Straight({195.0f, d.valve + 22.0f}, {195.0f, d.plate - 4.0f});
  const Vec2 lamp = DistrictLamps(world)[0];
  return DownPipe({195.0f, d.valve + 22.0f}, {lamp.x, lamp.y - 10.0f});
}

Cubic LinkPipe(int world) {
  if (world < 3 || world > kDistricts) return {};
  const Vec2 to{195.0f, Districts()[world - 1].valve - 22.0f};
  if (world == 3) return Straight({kFountain.x, kFountain.y + 18.0f}, to);
  if (world == 4) return Straight({195.0f, kLagoon.y1}, to);
  const Vec2 lamp = DistrictLamps(world - 1)[kDistrictLamps - 1];
  return DownPipe({lamp.x, lamp.y + 10.0f}, to);
}

float PipeFill(int stars, int gate) {
  if (gate <= 0) return 1.0f;
  return static_cast<float>(std::clamp(stars, 0, gate)) / static_cast<float>(gate);
}

// ---- The market square ----

Vec2 BottleSpot(int level) {
  level = std::clamp(level, 0, 14);
  return {87.0f + 54.0f * static_cast<float>(level % 5), kShelves[level / 5]};
}

Vec2 GlobeSpot(int challenge) {
  constexpr Vec2 kGlobes[kLobes] = {{118.0f, 2634.0f}, {195.0f, 2622.0f}, {272.0f, 2634.0f}};
  return kGlobes[std::clamp(challenge, 0, kLobes - 1)];
}

float ShopScroll(float view, bool globes) {
  // The mock's view (844 tall) stood at 2522 for the window, 2462 for the globes.
  return ClampScroll((globes ? 2628.0f : 2740.0f) - view * ((globes ? 166.0f : 218.0f) / kMockView), view);
}

float BellSwing(float seconds) {
  if (seconds < 0.0f || seconds > 2.4f) return 0.0f;
  return 24.0f * std::exp(-seconds / 0.65f) * std::sin(seconds / 0.085f);
}

const std::vector<LampTrail>& MarketTrail() {
  static const std::vector<LampTrail> trail = [] {
    const auto lamps = DistrictLamps(2);
    std::vector<LampTrail> out;
    for (int j = 0; j + 1 < 15; ++j) {
      const Vec2 a = lamps[j], b = lamps[j + 1];
      if (a.y == b.y) {  // along a row, from ring to ring
        const float dir = b.x > a.x ? 1.0f : -1.0f;
        out.push_back({j, Straight({a.x + 26.0f * dir, a.y}, {b.x - 26.0f * dir, b.y})});
      } else {  // round the row's end, bulging outwards
        const float out_x = a.x > 195.0f ? 44.0f : -44.0f;
        out.push_back(
            {j, {{a.x, a.y + 22.0f}, {a.x + out_x, a.y + 40.0f}, {b.x + out_x, b.y - 40.0f}, {b.x, b.y - 22.0f}}});
      }
    }
    out.push_back({14, {{330.0f, 3344.0f}, {330.0f, 3402.0f}, {300.0f, 3422.0f}, {288.0f, 3434.0f}}});
    for (int k = 0; k + 1 < kLobes; ++k) {
      const Vec2 a = lamps[15 + k], b = lamps[16 + k];
      out.push_back({15 + k, Straight({a.x - 28.0f, a.y}, {b.x + 28.0f, b.y})});
    }
    return out;
  }();
  return trail;
}

// ---- The canal quarter ----

namespace {

float Distance(Vec2 a, Vec2 b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y)); }

const art::Path& CanalPath() {
  static const art::Path path = [] {
    std::vector<glm::vec2> points;
    for (const Vec2 c : GrandCanal()) points.push_back({c.x, c.y});
    return art::MakePath(std::move(points));
  }();
  return path;
}

canal::Box Around(Vec2 a, Vec2 b, float by) {
  return {{std::min(a.x, b.x) - by, std::min(a.y, b.y) - by}, {std::max(a.x, b.x) + by, std::max(a.y, b.y) + by}};
}

canal::Box Grown(const Box& box, float by) { return {{box.x0 - by, box.y0 - by}, {box.x1 + by, box.y1 + by}}; }

}  // namespace

const std::vector<Vec2>& GrandCanal() {
  static const std::vector<Vec2> corners = {{195.0f, 3836.0f}, {195.0f, 3904.0f}, {75.0f, 3904.0f}, {75.0f, 3994.0f},
                                            {315.0f, 3994.0f}, {315.0f, 4154.0f}, {75.0f, 4154.0f}, {75.0f, 4314.0f},
                                            {315.0f, 4314.0f}, {315.0f, 4474.0f}, {75.0f, 4474.0f}, {75.0f, 4634.0f},
                                            {315.0f, 4634.0f}, {315.0f, 4734.0f}};
  return corners;
}

float CanalLength() { return CanalPath().Length(); }

std::vector<Vec2> CanalPart(float length, float step) {
  if (length <= 0.0f) return {};
  const art::Path part = CanalPath().Part(0.0f, std::min(length, CanalLength()));
  std::vector<Vec2> points;
  for (const glm::vec2 p : art::RoundCorners(part.points, kCanalBend, step)) points.push_back({p.x, p.y});
  return points;
}

float CanalAt(int lamp) {
  if (lamp >= 15) return CanalLength() + 30.0f + 10.0f * static_cast<float>(lamp - 15);
  const Vec2 at = DistrictLamps(3)[static_cast<size_t>(std::max(0, lamp))];
  const std::vector<Vec2>& canal = GrandCanal();
  float walked = 0.0f;
  for (size_t k = 0; k + 1 < canal.size(); ++k) {
    const Vec2 a = canal[k], b = canal[k + 1];
    const bool on = (a.x == b.x && at.x == a.x && at.y >= std::min(a.y, b.y) && at.y <= std::max(a.y, b.y)) ||
                    (a.y == b.y && at.y == a.y && at.x >= std::min(a.x, b.x) && at.x <= std::max(a.x, b.x));
    if (on) return walked + Distance(a, at);
    walked += Distance(a, b);
  }
  return walked;
}

float CanalTrail(int lamp) {
  if (lamp < 0 || lamp >= 15) return CanalLength();
  return std::min(CanalLength(), CanalAt(lamp));
}

Vec2 CanalPoint(float length) {
  const glm::vec2 p = CanalPath().PointAt(std::clamp(length, 0.0f, CanalLength()));
  return {p.x, p.y};
}

float PourSeconds(float from, float to) { return 0.4f + 0.0022f * std::fabs(to - from); }

const std::vector<QuarterRoof>& QuarterRoofs() {
  static const std::vector<QuarterRoof> roofs = [] {
    const std::vector<Vec2>& canal = GrandCanal();
    const auto lamps = DistrictLamps(3);
    std::vector<canal::Box> keep = {
        {{176.0f, 3560.0f}, {214.0f, 3844.0f}},  // the pipe from the fountain, the valve, the stub, the sluice
        {{170.0f, 3690.0f}, {292.0f, 3750.0f}},  // the valve's star plate
        Grown(kQuarterPlate, 6.0f),
        Grown(kLagoon, 4.0f),
        {{176.0f, kLagoon.y1}, {214.0f, kHeight}},  // the pipe on to World 4
    };
    for (const Vec2 park : kParks) keep.push_back(Around(park, park, kParkHalf.x + 10.0f));
    auto blocked = [&](const canal::Box& box) {
      if (box.max.y > 4996.0f) return true;  // the sanctum's ground starts under World 4's valve
      for (size_t k = 0; k + 1 < canal.size(); ++k) {
        if (Around(canal[k], canal[k + 1], 21.0f).Overlaps(box)) return true;
      }
      for (const Vec2 lamp : lamps) {
        if (Around(lamp, lamp, 34.0f).Overlaps(box)) return true;
      }
      return std::any_of(keep.begin(), keep.end(), [&](const canal::Box& k) { return k.Overlaps(box); });
    };
    std::vector<canal::Roof> grid;
    canal::Random random("The Canal Quarter");
    canal::Rooftops(random, {{-20.0f, kMarketBottom}, {380.0f, 4974.0f}}, blocked, grid);
    // Each roof between the plate and the lagoon belongs to its nearest harbour (squashed up and down: the rows
    // are farther apart than the harbours along them).
    std::vector<QuarterRoof> out;
    for (canal::Roof& roof : grid) {
      QuarterRoof q;
      const Vec2 c{(roof.box.min.x + roof.box.max.x) * 0.5f, (roof.box.min.y + roof.box.max.y) * 0.5f};
      if (c.y > kQuarterPlate.y1 && c.y < kLagoon.y0) {
        float best = 0.0f;
        for (int j = 0; j < 15; ++j) {
          const float dx = c.x - lamps[j].x, dy = (c.y - lamps[j].y) * 1.25f;
          const float d = std::sqrt(dx * dx + dy * dy);
          if (q.hood < 0 || d < best) q.hood = j, best = d;
        }
        q.distance = Distance(c, lamps[static_cast<size_t>(q.hood)]);
      }
      q.roof = std::move(roof);
      out.push_back(std::move(q));
    }
    return out;
  }();
  return roofs;
}

float HoodReach(int hood) {
  static const std::array<float, 15> reach = [] {
    std::array<float, 15> out{};
    for (const QuarterRoof& q : QuarterRoofs()) {
      if (q.hood >= 0) out[static_cast<size_t>(q.hood)] = std::max(out[static_cast<size_t>(q.hood)], q.distance);
    }
    return out;
  }();
  return hood >= 0 && hood < 15 ? reach[static_cast<size_t>(hood)] : 0.0f;
}

const std::vector<canal::Tree>& QuarterTrees() {
  static const std::vector<canal::Tree> trees = [] {
    std::vector<canal::Tree> out;
    canal::Random random("The Canal Quarter's parks");
    for (const Vec2 park : kParks) {
      for (int i = 0; i < 9; ++i) {
        canal::Tree tree;
        tree.at.x = park.x - kParkHalf.x + random.Next() * 2.0f * kParkHalf.x;
        tree.at.y = park.y - kParkHalf.y + random.Next() * 2.0f * kParkHalf.y;
        tree.r = 5.0f + random.Next() * 5.0f;
        out.push_back(tree);
      }
    }
    return out;
  }();
  return trees;
}

const std::vector<Box>& QuarterBridges() {
  static const std::vector<Box> bridges = [] {
    std::vector<Box> out;
    for (const Vec2 b : {Vec2{135, 3994}, Vec2{255, 4154}, Vec2{135, 4314}, Vec2{255, 4474}, Vec2{135, 4634}})
      out.push_back({b.x - 5.0f, b.y - 18.0f, b.x + 5.0f, b.y + 18.0f});  // across a row
    for (const Vec2 b : {Vec2{315, 4074}, Vec2{75, 4234}})
      out.push_back({b.x - 18.0f, b.y - 5.0f, b.x + 18.0f, b.y + 5.0f});  // across a bend down to the next row
    return out;
  }();
  return bridges;
}

const std::vector<Vec2>& QuayLamps() {
  static const std::vector<Vec2> lamps = [] {
    std::vector<Vec2> out;
    const art::Path& path = CanalPath();
    const std::vector<Vec2>& canal = GrandCanal();
    const auto harbours = DistrictLamps(3);
    for (float s = 40.0f; s < path.Length(); s += 38.0f) {
      const glm::vec2 p = path.PointAt(s), u = path.DirectionAt(s);
      const float side = static_cast<int>(s / 38.0f) % 2 ? 1.0f : -1.0f;
      const Vec2 lamp{p.x - u.y * 19.0f * side, p.y + u.x * 19.0f * side};
      bool clear = true;
      for (const Vec2 h : harbours) clear = clear && Distance(h, lamp) >= 36.0f;
      for (size_t k = 0; k + 1 < canal.size(); ++k) {  // not on the water round a bend
        const canal::Box water = Around(canal[k], canal[k + 1], 16.0f);
        clear = clear && !water.Contains(lamp);
      }
      if (clear) out.push_back(lamp);
    }
    return out;
  }();
  return lamps;
}

float QuarterScroll(int lamp, float view) {
  const Vec2 at = DistrictLamps(3)[static_cast<size_t>(std::clamp(lamp, 0, kDistrictLamps - 1))];
  return ClampScroll(at.y - view * (470.0f / kMockView), view);
}

// ---- The view ----

float ClampScroll(float y, float view) { return std::clamp(y, 0.0f, std::max(0.0f, kHeight - view)); }

float LanternScroll(int lantern, float view) {
  return ClampScroll(Lanterns()[lantern].at.y - view * (560.0f / kMockView), view);
}

float WindowScroll(float view) { return ClampScroll(kWindow.y - view * (278.0f / kMockView), view); }

float GateScroll(int world, float view) {
  if (world < 2 || world > kDistricts) return 0.0f;
  return ClampScroll(Districts()[world - 1].valve - view * (430.0f / kMockView), view);
}

bool WindowInView(float scroll, float view) { return scroll <= WindowScroll(view) + view * (60.0f / kMockView); }

// ---- What the map shows ----

std::array<Light, kLanterns> LanternLights(const MapLevels& levels, const StarMap& stars) {
  return WorldLights<kLanterns>(levels, stars, 1, kPetals);
}

std::array<Light, kDistrictLamps> DistrictLights(const MapLevels& levels, const StarMap& stars, int world) {
  return WorldLights<kDistrictLamps>(levels, stars, world, kDistrictLamps - kLobes);
}

int CurrentLantern(const std::array<Light, kLanterns>& lights) {
  for (int i = 0; i < kLanterns; ++i) {
    if (!lights[i].locked && !(lights[i].bits & kSolveStar)) return i;
  }
  return -1;
}

int MainStars(const MapLevels& levels, const StarMap& stars, int world) {
  const WorldLevels* w = FindWorld(levels, world);
  int count = 0;
  if (w) {
    for (const std::string& id : w->main) count += StarsIn(BitsOf(stars, id));
  }
  return count;
}

int StarsBefore(const MapLevels& levels, const StarMap& stars, int world) {
  int count = 0;
  for (int w = 1; w < world; ++w) count += MainStars(levels, stars, w);
  return count;
}

bool GateOpen(const MapLevels& levels, const StarMap& stars, int world) {
  if (world <= 1) return true;
  const WorldLevels* w = FindWorld(levels, world);
  return w && StarsBefore(levels, stars, world) >= w->gate;
}

int OpenGates(const MapLevels& levels, const StarMap& stars) {
  int last = 1;
  while (FindWorld(levels, last + 1) && GateOpen(levels, stars, last + 1)) ++last;
  return last;
}

float Dawn(int stars) { return std::clamp(static_cast<float>(stars) / 45.0f, 0.0f, 1.0f); }

std::vector<Color> LanternColors(const Level& level) {
  struct Cup {
    float x;
    Color want;
  };
  std::vector<Cup> cups;
  for (const Node& node : level.nodes) {
    if (node.kind == NodeKind::Cup) cups.push_back({node.at.x, node.want});
    if (node.kind == NodeKind::Rack) {
      for (const Slot& slot : node.slots) cups.push_back({node.at.x, slot.want});
    }
  }
  std::stable_sort(cups.begin(), cups.end(), [](const Cup& a, const Cup& b) { return a.x < b.x; });
  std::vector<Color> colors;
  for (const Cup& cup : cups) colors.push_back(cup.want);
  return colors;
}

// ---- What's new ----

Seen SeedSeen(const MapLevels& levels, const StarMap& stars) {
  const WorldLevels* first = FindWorld(levels, 1);
  auto early = [&](const std::string& id) {
    for (size_t i = 0; first && i < first->main.size() && i < 3; ++i) {
      if (first->main[i] == id) return true;
    }
    return false;
  };
  for (const auto& [id, bits] : stars) {
    if ((bits & kSolveStar) && !early(id)) return SeeAll(levels, stars);
  }
  return {};
}

News WhatsNew(const MapLevels& levels, const Seen& seen, const StarMap& stars) {
  News news;
  for (const WorldLevels& w : levels) {
    std::vector<const std::string*> ids;
    for (const std::string& id : w.main) ids.push_back(&id);
    for (const std::string& id : w.challenges) ids.push_back(&id);
    for (size_t i = 0; i < ids.size(); ++i) {
      const int fresh = BitsOf(stars, *ids[i]) & ~BitsOf(seen.stars, *ids[i]) & kMainStars;
      if (fresh) news.reveals.push_back({w.world, static_cast<int>(i), *ids[i], fresh});
    }
  }
  for (int world = seen.gates + 1; world <= OpenGates(levels, stars); ++world) news.gates.push_back(world);
  return news;
}

Seen SeeAll(const MapLevels& levels, const StarMap& stars) {
  Seen seen;
  for (const WorldLevels& w : levels) {
    for (const std::vector<std::string>* list : {&w.main, &w.challenges}) {
      for (const std::string& id : *list) {
        const int bits = BitsOf(stars, id) & kMainStars;
        if (bits) seen.stars[id] = bits;
      }
    }
  }
  seen.gates = OpenGates(levels, stars);
  return seen;
}

}  // namespace threadlock::city
