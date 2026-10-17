# Threadlock + WLWengine: project context for Claude Code

## What this is
**Threadlock** is the owner's second mobile game on his own C++ engine (**WLWengine**, a git submodule;
read `external/WLWengine/CLAUDE.md` for the engine's rules). The first game is Bluzzle
(`Desktop\dev\Bluzzle`, a block puzzle, on Google Play). The owner has a C++ background and learns
game-engine concepts by building games: explain concepts in chat, keep learning material out of the repo.
- **Mobile only:** Android first (then iOS). Desktop builds are only the engine's dev target; don't run or
  polish the desktop game. Pure C++ rules, solver and generator tests run on the PC.

## The game
**DESIGN.md is the design** (v3: the one rule, rules v3 with open sockets, the pieces and the piece x socket
table, machine parts, the city and its four worlds, stars, screens, sound, ads, the build order and the
decisions). In short: glowing liquid flows through a machine; the player places pieces in sockets (any
piece in any socket), taps play and watches. Different colours in one junction at once = **mud** (a race
condition); every piece is a concurrency idea (separator = branching, turnstile = mutex, tank = sleep,
flipper = round-robin, overflow = counting semaphore, bridge = don't share, mixer = join, twin gate =
barrier), every failure a real bug (mud, spill, dry, THREADLOCKED). Stars: Solve / Lean (≤ par pieces) /
Swift (≤ par time). Launch scope: worlds 1–4 (60 levels + 12 challenges), the daily puzzle and the Lab.
Each world is a trade of one city that runs on liquid light, with its own look and board grammar.

Competition (researched before the pitch): Auditorium (PC, old) is the closest; mobile pipe puzzles are
static. Zachtronics games (SpaceChem, Opus Magnum) are the model for "integer sim + animated view".

## History (Oct 2026)
- Earlier ideas the owner rejected: Epicycles (Fourier), Silhouette (shadows), Ripple (waves). Don't re-pitch.
- Engine work for this game, steps 1-5, all in the engine's main: scenes + viewports, blend modes, post passes,
  `fx::FlowParticles` (20k dots along paths), `scene::AddThreshold` (liquid), `app::Quality` (tiers).
- 2026-10-14: concept approved from a browser mock, then a one-level feel test ("Two Cups"). Concept mocks
  are one portrait phone screen, playable by touch.
- 2026-10-16: the full design (DESIGN.md), then the game built from it: rules v2, solver, generator,
  level tool, 72 levels, view v2, map, daily, Lab, saves, sounds, ads.
- 2026-10-16: owner feedback ("make it harder"; boards "flat and not artistic"; worlds must not share one
  base idea) led to v3, designed on a claude.ai canvas in three rounds and approved: open sockets, machine
  parts (turning rack, mills + gates), four worlds with their own looks. Built step by step (DESIGN.md §11).

## Layout
```
src/threadlock_rules.{h,cpp}  rules v3: liquids/beads, pieces + Role (piece x socket), the level graph (incl. racks,
                              mills, gates), Board (the simulation). No engine includes.
src/level_io.{h,cpp}          level files (JSON) + the pars file; WriteLevel (tool: format, search)
src/solver.{h,cpp}            every placement where each piece acts, run each: pars (Lean/Swift), solutions, NextHint
src/generator.{h,cpp}         daily puzzle + Lab: 4 templates (race, shares, mixer, shared pipe), solver-filtered
src/catalog.{h,cpp}           loads assets/levels/world*.json + pars.json (platform::ReadFile)
src/progress.{h,cpp}          saves (kit::SaveStore): progress.json (mergeable: star bits, counters, daily log,
                              achievements) + device.json (settings, hints, ad pacing, Lab); unlock rules
src/threadlock_app.{h,cpp}    the one System: services (catalog, progress, feedback, liquid, ads) + navigation, toasts,
                              debug panel (WLW_DEBUG_UI)
src/level_screen.{h,cpp}      the level screen: drag/tap/long-press, run loop (1x/2x/4x), result card, Show me, hints
src/map_screen.{h,cpp}        the scrolling map (worlds, star gates), daily + Lab cards
src/menus.{h,cpp}             overlays: settings (palettes), achievements, daily (streak, archive), Lab
src/board_view.{h,cpp}        a board on screen: per-node animations, markers, hit tests, and what every world shares:
                              the pieces (DrawPiece = the brass toolkit), sockets, flaps, hints. Calls the world's art
src/board_art.{h,cpp}         BoardArt: a world's way of drawing a machine (back on the bloomed Picture layer, liquid
                              placement, fronts on the Overlay); ArtFrame = one frame's state; Layout
src/world_look.h              per world: which art + the HUD/card tint (accent, panel, EDIT gradient); LookForWorld
src/lantern_art.{h,cpp}       World 1, The Lantern Works: rose window + wall (assets/shaders/lantern_wall.shader) and
                              glass drawn once into an offscreen viewport (redrawn when the level, screen or the
                              window's light changes); glass tubes, coils, drip falls, globes, crucibles, lanterns
src/workshop_art.{h,cpp}      the v2 look (worlds 2-4 until their art, the daily, the Lab) + shared machine parts
src/art_shapes.{h,cpp}        drawing helpers: paths with round bends, Ribbon (seamless translucent bands), strokes,
                              glyphs, glows, brass (medallion, plate, bar)
src/liquid.{h,cpp}            metaball liquid: blobs per drop from the rules' state -> SpriteBatch -> RGBA16F viewport
                              (blur + threshold) -> Picture layer, bloom on the screen
src/audio_synth.{h,cpp}       every sound synthesized at startup (instruments per colour, chords, thud, grind...)
src/feedback.{h,cpp}          sounds + haptics for board events
src/palette.h, ui_parts.h     colours/palettes, theme, small UI helpers
src/main.cpp                  MakeApp() + WLW_MAIN
assets/levels/                world1-4.json (handmade levels) + pars.json (generated by the tool: don't edit by hand)
assets/shaders/               canvas shaders: lantern_wall (World 1's backdrop), ui_gradient (the EDIT / PLAY button)
tools/threadlock_tool.cpp     the level tool (below)
tests/                        doctest: rules, sockets (the piece x socket table), machine parts, solver, content
                              (every shipped level + generated ones)
android/                      Gradle project: ThreadlockActivity (WlwActivity + AdMobService)
external/WLWengine/           the engine (pinned at v0.1.0)
```

## Rules v3 (threadlock_rules.h)
- A `Level` is a graph of `Node`s (Spout, Fork, Pass = pipe socket, Cross, Merge, Cup, Drain, and machine parts:
  Rack, Mill, Gate) joined by one-way `Pipe`s drawn as polylines. `Level::Finalize()` derives everything else: a
  pipe's cells = round(drawn length between node edges / 10), node dwell (junction and mill 3, fork/pass/gate 1),
  crossing lanes (each in pairs with the straightest out). Board units: 360 x 600, y down.
- **Open sockets**: any piece in any socket (fork, pass, cross, merge). `Role(piece, node)` = Acts / Idle / Trap,
  the table in DESIGN.md §3. Mud: a crossing or merge is shared unless a turnstile or a mixer (or a bridge on a
  crossing) sits there (`Board::Shared`). A fork follows its flap unless a router (separator, flipper, overflow)
  sits there (`FlapInCharge`). Routers on a crossing route whatever the lane; a mixer on a crossing sends each
  drop on along its own lane; a twin gate on a crossing/merge lets both lanes' front drops in on one tick; a
  mixer or twin gate with one way in is a trap (waits forever).
- Machine parts: a Rack's drop lands in bottle (first + t / period) % K (each bottle a cup: spill / dry / wrong
  per bottle, `Outcome::fail_slot`); a Mill (one drop at a time) flips the Gates in its `gears` as a drop leaves,
  applied at the end of the tick; a shut Gate admits nothing.
- `Board::Tick()`: dwell timers count down, then every move that can happen this tick is resolved to a fixed
  point (a drop leaving makes room for the one behind in the same tick: queues move together, a lock frees on
  the tick its drop leaves). Contested places: turnstile arrow, then lane order; exits by entry order. Then gate
  flips, collisions (different beads inside a shared crossing/merge = mud), then the end (all landed, or frozen
  = THREADLOCKED with a JamReport: stuck nodes, the cycle, a lonely mixer/twin gate or a gate nothing opens).
- The solver tries only placements where every piece acts (an idle or trapped piece can't beat the same
  placement without it); `solutions` counts those.
- Events for the view/sound: Pour, Enter, Leave, Split, Mix, Collide, NearMiss, Wait, Land, Spill, CupFull, Flip,
  Turn, Finished, Jammed. `Outcome` = the first failure (kind, node, bottle, tick) for "Show me".
- Deadlocks that don't depend on turnstile arrows need one-cell links between locks (3-8, 3-11, 3-13): with
  longer links some arrow setting always lets traffic through (priority only decides same-tick arrivals).
- **Determinism across devices** (pars are baked on the PC, the daily is generated on each phone and must be
  the same for everyone): pipe cells use doubles + plain `sqrt` (never `std::hypot` / float sums), and the
  generator makes one random draw per statement (C++ leaves argument order unspecified: MSVC and clang differ).
  The test "generated levels are the same on every device" pins fingerprints; run the tests on a phone too.

## The level tool (PC)
```
threadlock_tool check                  every level: valid, solvable, pars current, empty board fails
threadlock_tool bake                   solve all, write assets/levels/pars.json (run after any level change)
threadlock_tool format                 rewrite the world files in one layout (a node per line)
threadlock_tool solve <id>             cells per pipe, every solution, how small placements fail
threadlock_tool timing <id> [lean|swift]   when each drop enters each junction
threadlock_tool render <id> <png> [none|lean|swift] [tick]   the board (+ drops at a tick): check layouts
threadlock_tool daily <yyyymmdd> <png> / lab <seed> <easy|medium|hard> <png>
threadlock_tool search <kind> <difficulty> <seed> <count> <min pieces> <max solutions> [png dir]
                                       generated machines as level JSON (a start for hard levels)
```
Level design loop: draft JSON -> `solve`/`render` -> move bends (cells follow length) -> `bake` -> tests.
Pipe shapes: an object after the bends, `["s", "c", [[x, y], ...], {"coil": [segment, turns], "drip": segment}]`
(segment k = point k to k+1 of from, bends..., to). A coil winds its segment round its middle (`CoilPoints`: a turn is
~70 long, ~5 cells more than straight, so it changes the timing and the pars); a drip is look only (drops fall
through the air: nozzle, funnel).

## Build / test / run (Windows PC)
PC tests and the tool without CMake/vcpkg, from a VS developer shell (repo root):
```
cl /nologo /std:c++20 /EHsc /W4 /WX /O2 /external:W0 /external:I external\WLWengine\third_party\tinygltf ^
   /external:I external\WLWengine\third_party\doctest /Fe:build\pc\threadlock_tests.exe ^
   tests\*.cpp src\threadlock_rules.cpp src\level_io.cpp src\solver.cpp src\generator.cpp
(the tool: tools\threadlock_tool.cpp + the same four sources, plus /external:I ...\third_party\stb)
```
CMake (desktop) also defines `threadlock_tests` and `threadlock_tool`; those need the engine's vcpkg submodule
(`git submodule update --init --recursive`). VS is at `C:\Program Files\Microsoft Visual Studio\18\Community`
(vswhere fails, harmless): `Import-Module "$vs\Common7\Tools\Microsoft.VisualStudio.DevShell.dll";
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments "-arch=x64"`.
Format with the engine's clang-format and style (the code follows it; this repo's own .clang-format doesn't
match the code): `clang-format -i --style=file:<engine>/.clang-format src/* tests/* tools/*`.

The same tests on a phone (ARM64, the NDK's clang), from Git Bash with `MSYS_NO_PATHCONV=1`:
```
$NDK/toolchains/llvm/prebuilt/windows-x86_64/bin/clang++.exe --target=aarch64-linux-android24 -std=c++20 -O2 \
  -static-libstdc++ -isystem external/WLWengine/third_party/tinygltf -isystem external/WLWengine/third_party/doctest \
  tests/*.cpp src/threadlock_rules.cpp src/level_io.cpp src/solver.cpp src/generator.cpp -o build/android-tests/threadlock_tests
adb -s <serial> push build/android-tests/threadlock_tests /data/local/tmp/tl/   (+ assets/levels/*.json to /data/local/tmp/tl/assets/levels/)
adb -s <serial> shell "cd /data/local/tmp/tl && ./threadlock_tests"
```

Android (JAVA_HOME = a JDK 21 from `%USERPROFILE%\.jdks\jbr-21*`):
```
cd android; .\gradlew.bat assembleDebug -q
adb -s 9B221FFAZ002VK install -r app\build\outputs\apk\debug\app-debug.apk
adb -s 9B221FFAZ002VK shell am start -n com.aboelbisher.threadlock/.ThreadlockActivity
adb -s 9B221FFAZ002VK logcat -s WLW
```
Always `adb -s <serial>`: the owner's Pixel 4 (9B221FFAZ002VK, PIN-locked when idle: ask the owner to unlock
it) has Bluzzle with saves (never uninstall it). Ask before using an emulator. Screenshots:
`cmd /c "adb -s <serial> exec-out screencap -p > file.png"` (PowerShell's `>` corrupts PNGs). Wake the screen first
(`adb shell input keyevent KEYCODE_WAKEUP`). Dev cvars to reach a state without tapping (quote the whole am command):
`adb shell "am start -n com.aboelbisher.threadlock/.ThreadlockActivity --es wlw 'tl.open=1-4;tl.solve=1;tl.autoplay=1'"`
(open a level, place its Lean solution, tap play after 1 s).
Ads: AdMob through the kit; **Google's sample app id and test units** until Threadlock has its own (manifest +
MakeAdsConfig in threadlock_app.cpp). The debug build has the ImGui panel (Threadlock window: jump to any level,
open everything, hints, wipe, ads).

## How the owner works
- Design first: concept + API + "how big engines do it", then wait for his go. Then build, verify (tests,
  Android build, phone screenshots), show the results, and wait for approval before committing.
- Small focused commits; never amend or force-push pushed commits. Don't delete files without asking.
- Engine changes go to the engine repo first (`Desktop\dev\WLWengine`, also used by Bluzzle), then bump
  the submodule here (and in Bluzzle).
- Research competitors before pitching new game ideas.

## Next steps
1. Play it on the Pixel 4 (checked 2026-10-16: levels 1-1..1-4, mud + Show me, the solved/failed cards, the
   map, daily, settings, hints, the 3-8 THREADLOCKED picture all work). Still to tune: feel, the liquid look,
   performance per quality tier; check Lab, achievements, palettes and ads in play.
2. Playtest World 1 with 5-10 people (DESIGN.md §11 step 4); adjust levels with the tool.
3. Launch work: Threadlock's own AdMob app + units, analytics (Firebase via a kit bridge), store page, closed
   test. Then worlds 5-8 as updates (collector, bell, gates, layered glasses, ink, clock gates, rings).
4. A desktop level editor (Dear ImGui, live run) would speed up level design beyond the tool.
