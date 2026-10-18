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
      {2, 2500.0f, 3480.0f, "THE MARKET SQUARE", 2412.0f, 2605.0f, 2700.0f, 3360.0f, 0.3f},
      {3, 3480.0f, 4440.0f, "THE CANAL QUARTER", 3440.0f, 3590.0f, 3690.0f, 4330.0f, 1.4f},
      {4, 4440.0f, kHeight, "THE SANCTUM", 4410.0f, 4560.0f, 4670.0f, 5320.0f, 2.6f},
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
  const Vec2 lamp = DistrictLamps(world)[0];
  return DownPipe({195.0f, Districts()[world - 1].valve + 22.0f}, {lamp.x, lamp.y - 10.0f});
}

Cubic LinkPipe(int world) {
  if (world < 3 || world > kDistricts) return {};
  const Vec2 lamp = DistrictLamps(world - 1)[kDistrictLamps - 1];
  return DownPipe({lamp.x, lamp.y + 10.0f}, {195.0f, Districts()[world - 1].valve - 22.0f});
}

float PipeFill(int stars, int gate) {
  if (gate <= 0) return 1.0f;
  return static_cast<float>(std::clamp(stars, 0, gate)) / static_cast<float>(gate);
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
