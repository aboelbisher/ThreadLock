#pragma once

#include <array>
#include <string>
#include <vector>

#include "canal_look.h"
#include "stars.h"
#include "threadlock_rules.h"

// The city map (DESIGN.md §5, §8): the level select. One tall picture of the city, read top to bottom:
// World 1's tower (the rose window, then a path of numbered lanterns, one per level, and the challenges'
// diamonds on a side branch), then the market square, the canal quarter and the sanctum (worlds 2-4),
// each behind a brass valve that opens with stars. The window isn't a button: petal n lights when level
// n is solved (its big pane in the level's lantern colours, the two small panes for Lean and Swift),
// the heart's three lobes with the challenges.
//
// Plain C++ (no engine), tested on the PC: where everything stands, what is lit, and what is new since
// the player last saw the map (the seen record), which the map animates.
//
// Map units: kWidth wide (the screen's width; the screen scales them), y down, kHeight tall. The layout
// was drawn for a 390 x 844 view (the approved mock).
namespace threadlock::city {

inline constexpr float kWidth = 390.0f;
inline constexpr float kHeight = 5968.0f;
inline constexpr float kMockView = 844.0f;  // the view height the scroll targets were tuned on

inline constexpr int kPetals = 15;  // World 1's main path
inline constexpr int kLobes = 3;    // its challenges
inline constexpr int kLanterns = kPetals + kLobes;
inline constexpr int kDistrictLamps = 18;  // worlds 2-4: 15 + 3 challenges

// ---- The rose window ----
inline constexpr Vec2 kWindow{195.0f, 468.0f};
inline constexpr float kWindowRim = 188.0f;     // the brass ring
inline constexpr float kWindowInside = 177.0f;  // the dark glass inside it
inline constexpr float kHeart = 66.0f;          // the heart's disc
inline constexpr float kSmallInner = 76.0f;     // the Lean / Swift panes: kSmallInner..kSmallOuter
inline constexpr float kSmallOuter = 100.0f;
inline constexpr float kTipInner = 104.0f;  // the big pane: kTipInner out to its pointed tip
inline constexpr float kTipShoulder = 148.0f;
inline constexpr float kTipBulge = 172.0f;
inline constexpr float kPetalHalf = 10.5f;  // degrees
inline constexpr float kLobeRing = 31.0f;   // the heart's lobes: centres this far out, kLobeRadius each
inline constexpr float kLobeRadius = 23.0f;

float PetalAngle(int petal);              // degrees, -90 = up, clockwise on screen; petal 0 = 1-1 at the top
Vec2 Polar(float radius, float degrees);  // around the window's centre

// A ring-shaped strip of glass (a pane): pairs of points at the same angle; draw it as the convex quads
// inner[k], outer[k], outer[k + 1], inner[k + 1].
struct Band {
  std::vector<Vec2> inner, outer;
};
Band PetalTip(int petal, int steps);                   // the big pane
Band PetalSliver(int petal, int k, int n, int steps);  // the k-th of its n colour slivers, left to right
Band LeanPane(int petal, int steps);                   // the small pane before the petal's middle
Band SwiftPane(int petal, int steps);                  // and after it
float TipOuter(int petal, float degrees);              // how far out the big pane reaches at an angle
Vec2 LobeCentre(int lobe);

// ---- The lantern path (World 1's level select) ----
struct Lantern {
  Vec2 at;
  bool diamond = false;  // a challenge
  float radius = 27.0f;  // the glass
  float ring = 35.0f;    // the brass ring (and the breathing halo)
  float hit = 36.0f;     // a tap within this (a diamond: |dx| + |dy| <= hit)
  float stars_y = 0.0f;  // the row of its three stars
};
inline constexpr float kStarSpacing = 16.0f;
inline constexpr float kStarRadius = 6.5f;
const std::array<Lantern, kLanterns>& Lanterns();
int HitLantern(Vec2 at);  // the lantern a tap at `at` (map units) picks, -1 if none

// The dotted trail between lanterns: 14 pieces down the main path, then 1-15 -> C1 -> C2 -> C3. A piece
// lights once the lantern it leaves from is lit.
struct TrailPiece {
  int from = 0, to = 0;  // lanterns
};
const std::vector<TrailPiece>& Trail();
std::vector<Vec2> TrailPoints(int piece, int samples);  // samples + 1 points along it

// ---- Districts ----
struct District {
  int world = 1;
  float top = 0.0f, bottom = 0.0f;  // the stretch of the map it owns
  const char* place = "";           // its name plate's second line
  float valve = 0.0f;               // the gate's brass valve (world > 1)
  float plate = 0.0f;               // the name plate's top
  float lamps_top = 0.0f, lamps_bottom = 0.0f, phase = 0.0f;
};
inline constexpr int kDistricts = 4;
const std::array<District, kDistricts>& Districts();
int DistrictAt(float y);  // the world whose stretch holds map y

// Worlds 2-4 asleep in mist: their levels as lamps, 15 on a winding path then 3 challenge diamonds.
// Until each world gets its own art they stay playable: an open district's lamps are buttons. World 2's
// lamps stand in three rows of five across the market square (the second right to left), then the
// challenges' diamonds, right to left. World 3's are harbours on the Grand Canal, five rows of three
// (every other row right to left), then the buoys in the lagoon, right to left.
std::array<Vec2, kDistrictLamps> DistrictLamps(int world);
inline constexpr float kLampHit = 30.0f;
int HitLamp(int world, Vec2 at);  // the lamp a tap picks, -1 if none

struct Cubic {
  Vec2 a, b, c, d;
  Vec2 At(float t) const;
  std::vector<Vec2> Points(int samples) const;  // samples + 1
};
// The valve down to the district's first lamp (worlds 2 and 3: a short stub into its name plate).
Cubic GatePipe(int world);
// The district before it down to this one's valve (its last lamp; World 2's fountain, World 3's lagoon); world >= 3.
Cubic LinkPipe(int world);
// The tower's pipe, from under the lantern path down to World 2's valve: it fills with stars toward the gate.
inline constexpr Vec2 kTowerPipeTop{195.0f, 2050.0f};
inline constexpr Vec2 kTowerPipeBottom{195.0f, 2392.0f};
float PipeFill(int stars, int gate);  // 0..1

// ---- The market square (World 2's district; the approved mock MarketSquare.dc.html, map y = mock y + 2262) ----
// Under the plate, the apothecary's shop front: its window is World 2's progress picture, one bottle per
// level on three shelves (a solve fills it in the level's colours, Lean corks it, Swift labels it, all
// three seal it in gold foil), three show globes in the fanlight for the challenges, the door bell on its
// bracket. Then the cobbled square with its stalls, the lamps, and the fountain the next pipe leaves from.
struct Box {
  float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
};
inline constexpr Box kShopFront{20.0f, 2518.0f, 370.0f, 2962.0f};  // cornice to step
inline constexpr Box kShopSign{72.0f, 2542.0f, 318.0f, 2580.0f};
inline constexpr Box kShopWindow{60.0f, 2666.0f, 330.0f, 2938.0f};
inline constexpr float kFanlightTop = 2590.0f;  // the arch over the window: a half ellipse 135 x 52 from y 2642
inline constexpr float kShelves[3] = {2752.0f, 2842.0f, 2930.0f};
inline constexpr Vec2 kBellPivot{350.0f, 2600.0f};
inline constexpr Vec2 kFountain{195.0f, 3580.0f};  // the basin's middle
inline constexpr float kMarketBottom = 3622.0f;    // the square's last cobbles
Vec2 BottleSpot(int level);                        // level 0..14: the bottle's bottom middle on its shelf
Vec2 GlobeSpot(int challenge);
// The scroll that shows the shop window (or its fanlight's globes) with the square's first lamps.
float ShopScroll(float view, bool globes);
// The door bell's swing `seconds` after it rang, degrees (0 once it has settled).
float BellSwing(float seconds);

// The dotted trail between World 2's lamps, one piece per pair (lit once the lamp it leaves from is):
// along each row, round the ends, 2-15 down to C1, C1 -> C2 -> C3.
struct LampTrail {
  int from = 0;
  Cubic curve;
};
const std::vector<LampTrail>& MarketTrail();

// ---- The canal quarter (World 3's district; the approved mock CanalQuarter.dc.html, map y = mock y + 3574) ----
// The quarter seen from above at night. Under the plate a sluice lets the Grand Canal in; it winds down past
// fifteen harbours, one per level (its lamp is the button, the rooftops round it are the level's
// neighbourhood), into the lagoon, where three buoys are the challenges. A solve lights its neighbourhood and
// the canal on to the next harbour.
inline constexpr Box kQuarterPlate{85.0f, 3760.0f, 305.0f, 3810.0f};
inline constexpr Box kSluice{180.0f, 3822.0f, 210.0f, 3842.0f};
inline constexpr Box kLagoon{28.0f, 4724.0f, 362.0f, 4852.0f};
inline constexpr float kLagoonCorner = 40.0f;
inline constexpr float kCanalBend = 22.0f;  // the Grand Canal's round bends
inline constexpr Vec2 kParks[2] = {{195.0f, 4074.0f}, {195.0f, 4394.0f}};
inline constexpr Vec2 kParkHalf{30.0f, 15.0f};
const std::vector<Vec2>& GrandCanal();  // its corners, from the sluice to the lagoon
float CanalLength();                    // along its corners
// The first `length` of the canal, its bends rounded (sampled every `step` along an arc).
std::vector<Vec2> CanalPart(float length, float step);
// How far along the canal a lamp stands (a buoy: past its end, out in the lagoon).
float CanalAt(int lamp);
// How far the canal is lit while `lamp` is the next to play: down to it (-1, or a challenge: all the way to the
// lagoon).
float CanalTrail(int lamp);

struct QuarterRoof {
  canal::Roof roof;
  int hood = -1;  // whose neighbourhood it is: the nearest harbour on the main path (-1: by the plate or the lagoon)
  float distance = 0.0f;  // from that harbour's lamp
};
const std::vector<QuarterRoof>& QuarterRoofs();  // top to bottom, clear of the canal, lamps, parks, pipes and plates
float HoodReach(int hood);                       // its farthest roof
const std::vector<canal::Tree>& QuarterTrees();  // in the two parks
const std::vector<Box>& QuarterBridges();        // over the canal
const std::vector<Vec2>& QuayLamps();            // along the canal, sides in turn
// The scroll that shows a lamp just past the middle of the view (where its news plays).
float QuarterScroll(int lamp, float view);

// ---- The view ----
// Scroll (map y at the top of a view `view` tall) that shows lantern i, the window, or the gate of a world.
float LanternScroll(int lantern, float view);
float WindowScroll(float view);
float GateScroll(int world, float view);
float ClampScroll(float y, float view);
bool WindowInView(float scroll, float view);  // most of the window is on screen

// ---- What the map shows ----
// The levels the map knows, in map order (the screen fills it from the catalog).
struct WorldLevels {
  int world = 1;
  int gate = 0;  // stars before the world needed to open it
  std::vector<std::string> main, challenges;
};
using MapLevels = std::vector<WorldLevels>;

// A lamp's or lantern's state as drawn.
struct Light {
  int bits = 0;  // StarBits
  bool locked = true;
};
// World 1's lanterns (15 then the challenges): a main lantern is open once the one before it is solved,
// the challenges once 1-15 is; every one is locked while its world's gate is shut.
std::array<Light, kLanterns> LanternLights(const MapLevels& levels, const StarMap& stars);
std::array<Light, kDistrictLamps> DistrictLights(const MapLevels& levels, const StarMap& stars, int world);
// The lantern that breathes (the next level to play): the first unsolved one on the main path, then the
// challenges; -1 when everything is solved.
int CurrentLantern(const std::array<Light, kLanterns>& lights);
int MainStars(const MapLevels& levels, const StarMap& stars, int world);  // the main path's stars
int StarsBefore(const MapLevels& levels, const StarMap& stars, int world);
bool GateOpen(const MapLevels& levels, const StarMap& stars, int world);
int OpenGates(const MapLevels& levels, const StarMap& stars);  // the last world whose gate is open (1 = none)

// The sky's dawn (0 = night, 1 = sunrise), from all the stars on the map.
float Dawn(int stars);
std::vector<Color> LanternColors(const Level& level);  // its cups (rack bottles too), left to right

// ---- What's new ----
// What the map last showed this device (device.json): every level's stars as they were drawn, and the
// last world whose gate was seen opening.
struct Seen {
  StarMap stars;
  int gates = 1;
};
// No record yet (an old save or a restored one): what the map shows as already seen. The first visit
// after 1-3 (nothing solved past World 1's first three levels) sees nothing, so the three light up;
// otherwise everything is seen (a restored save doesn't replay a hundred solves).
Seen SeedSeen(const MapLevels& levels, const StarMap& stars);

struct Reveal {
  int world = 1;
  int index = 0;  // a lantern (World 1) or a lamp: main path then challenges
  std::string level;
  int fresh = 0;  // star bits to light (kSolveStar, kLeanStar, kSwiftStar)
};
struct News {
  std::vector<Reveal> reveals;  // in map order
  std::vector<int> gates;       // worlds whose gate opened, in order
  bool Empty() const { return reveals.empty() && gates.empty(); }
};
News WhatsNew(const MapLevels& levels, const Seen& seen, const StarMap& stars);
Seen SeeAll(const MapLevels& levels, const StarMap& stars);  // the record once the news has played

}  // namespace threadlock::city
