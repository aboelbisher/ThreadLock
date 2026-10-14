# Threadlock + WLWengine: project context for Claude Code

## What this is
**Threadlock** is the owner's second mobile game on his own C++ engine (**WLWengine**, a git submodule;
read `external/WLWengine/CLAUDE.md` for the engine's rules). The first game is Bluzzle
(`Desktop\dev\Bluzzle`, a block puzzle, on Google Play). The owner has a C++ background and learns
game-engine concepts by building games: explain concepts in chat, keep learning material out of the repo.
- **Mobile only:** Android first (then iOS). Desktop builds are only the engine's dev target; don't run or
  polish the desktop game. Pure C++ rule tests run on the PC.

## The game
A fluid "routing" puzzle for regular people. Glowing liquid flows through pipes; the player places
physical-looking pieces in sockets, then taps play and watches. Each piece is a concurrency idea, but
players never see those words:
| Piece | What it does | The idea behind it |
|---|---|---|
| Colour filter | splits the mixed liquid: blue one way, amber the other | branching |
| Turnstile | one blob through at a time, the others wait | mutex |
| Tank | holds a blob for a moment, then lets it go | buffer |
| Counter gate (later) | opens after N blobs have arrived | semaphore |
| Mixer (later) | outputs a new colour only when both inputs are there | logic gate |
Two colours inside a crossing at the same time = **mud** (a race condition). Cups want one pure colour.
Play is **place, then watch** (deterministic: the same pieces always give the same run).

Level 1, "Two Cups": the source pours a blue+amber mix; the cups are swapped (blue cup right, amber
left), so the two streams must cross. Filter only -> every pour's halves meet at the crossing -> mud.
Solutions: filter + turnstile at the crossing, or filter + tank on the amber lane (or all three).

Competition (researched before the pitch): Auditorium (PC, old) is the closest; mobile pipe puzzles are
static. Zachtronics games (SpaceChem, Opus Magnum) are the model for "integer sim + animated view".

## History (Oct 2026)
- Earlier ideas the owner rejected: Epicycles (Fourier), Silhouette (shadows), Ripple (waves). Don't re-pitch.
- Engine work for this game, steps 1-5, all in the engine's main: scenes + viewports, blend modes, post passes,
  `fx::FlowParticles` (20k dots along paths), `scene::AddThreshold` (liquid), `app::Quality` (tiers).
- 2026-10-14: concept approved from a browser mock, then this feel test was built. The owner's verdict:
  "the concept is good". The first mock was a desktop-style page and got "we're talking about a mobile
  game here": concept mocks are one portrait phone screen, playable by touch.

## Layout
```
src/threadlock_rules.{h,cpp}  the rules: plain C++, no engine includes (tested on their own)
src/threadlock_app.{h,cpp}    the game's one wlw::app::System: input, drawing, the liquid
src/main.cpp                  MakeApp() + WLW_MAIN
tests/threadlock_rules_tests.cpp
assets/fonts/                 LilitaOne (OFL), the same font as Bluzzle
android/                      Gradle project: ThreadlockActivity (WlwActivity, no SDK services yet)
external/WLWengine/           the engine
```

## Rules model (threadlock_rules.h)
- A `Level` is a graph: `Node`s (Source, Pass, Split, Cross, Cup; `socket` = a piece may go there;
  `dwell` = ticks a blob spends inside, the crossing is 3 = one blob long) joined by `Segment`s measured in
  cells. `TwoCups::Make()` builds level 1 and names its node / segment ids.
- `Board::Tick()`: (1) blobs in nodes count down and leave (a Split makes two halves: Blue/Amber with a
  filter, else Mud/Mud); (2) blobs on segments move one cell, front first, or enter the end node (a busy
  turnstile = wait); (3) the source pours every `pour_every` ticks; (4) two lanes inside a Cross at once =
  all mud (`Collide`). Returns events: EnterSegment, EnterNode (with the segment it came by), Collide,
  CupFilled, Finished. Integers only, so runs are deterministic (a test checks it).
- A tank adds `tank_hold` (6) ticks. A turnstile adds no time; it only makes others wait. (A first
  version gave it turning time, and then a turnstile on the amber lane alone solved the level by delay.
  The tests caught it.)
- `Blocked(segment)`: a blob waiting at its end, or held by a tank there. The view piles dots up there.

## View (threadlock_app.cpp)
- Board units: a 360 x 760 portrait board, y down, centred in the UI canvas (reference 360 x 800, so the
  status bar fits). The liquid camera shows canvas units (world = (x, -y)), so dots line up with the pieces.
- Liquid: `FlowParticles` -> `SpriteBatch` -> offscreen RGBA16F viewport with blur + threshold -> drawn as
  an image on a Picture canvas layer (pipes under it), with bloom on the screen. Pieces, cups and text are
  on the Overlay canvas.
- One path per rules segment, speed = length / (cells x 0.125 s tick). **Junctions re-emit** (like Niagara
  event handlers): on EnterSegment the view emits a burst of dots (40 x density) at the path's start; the
  previous path's dots fade at its dead end under the piece. The crossing has a short path per lane.
  `SetStop` at a path's end while the rules say Blocked. Cups = a wide path with a stop at the bottom, so
  arriving dots queue and fill the cup. The source = two self-looping circles.
- Quality tiers (`App().Quality()`, 3 levels): density 0.3 / 0.6 / 1.0, liquid scale 0.35 / 0.5 / 0.5,
  bloom 0.125 / 0.25 / 0.25, dot size 13 / density^0.25.
- Input: drag pieces from the tray to a socket (the piece floats 56 units above the finger; it snaps
  within 52); dropping on a filled socket sends that piece back to the tray; a tap leaves it in place.
  The round button: play / stop / back to editing.

## Build / test / run (Windows PC)
Rules tests (no vcpkg needed), from a VS developer shell:
```
cl /nologo /std:c++20 /EHsc /W4 /WX tests\threadlock_rules_tests.cpp src\threadlock_rules.cpp
```
(CMake also defines `threadlock_rules_tests` for desktop builds; those need the engine's vcpkg submodule:
`git submodule update --init --recursive`.)
VS is at `C:\Program Files\Microsoft Visual Studio\18\Community` (vswhere fails, harmless):
`Import-Module "$vs\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"; Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments "-arch=x64"`.

Android (JAVA_HOME = a JDK 21 from `%USERPROFILE%\.jdks\jbr-21*`):
```
cd android; .\gradlew.bat assembleDebug -q
adb -s emulator-5554 install -r app\build\outputs\apk\debug\app-debug.apk
adb -s emulator-5554 shell am start -n com.aboelbisher.threadlock/.ThreadlockActivity
adb -s emulator-5554 logcat -s WLW
```
Always install with `adb -s <serial>`: the owner's Pixel 4 (serial 9B221FFAZ002VK) may be plugged in and
has Bluzzle with saves (never uninstall it). Screenshots: `cmd /c "adb exec-out screencap -p > file.png"`
(PowerShell's `>` corrupts PNGs). Emulator (1080 x 2400): board point (x, y) = pixels (3x, 3(y + 9.2));
e.g. tray filter (186, 2164), play button (900, 2164); drag with `input swipe x1 y1 x2 y2 900` to a finger
point 56 units below the socket.

## How the owner works
- Design first: concept + API + "how big engines do it", then wait for his go. Then build, verify
  (tests, Android build, emulator screenshots), show the results, and wait for approval before committing.
- Small focused commits; never amend or force-push pushed commits. Don't delete files without asking.
- Engine changes go to the engine repo first (`Desktop\dev\WLWengine`, also used by Bluzzle), then bump
  the submodule here (and in Bluzzle).
- Research competitors before pitching new game ideas.

## Next steps
1. Try it on the Pixel 4 (separate app id, safe next to Bluzzle): feel, readability, and performance
   (GPU ms per tier; the emulator has no GPU timer). Optimize FlowParticles' WriteTo first if hot.
2. Look: each pour reads as separate beads, not a stream; cup liquid edges are ragged. Tune blob length /
   dot counts / threshold, or make a pour a longer stream.
3. More levels with the later pieces (counter gate, mixer); a level format; then menus, save, sound.
4. Engine ideas if the look needs them: an on_arrive hook / per-colour routing in FlowParticles (instead
   of re-emitting at junctions), thermal input for Quality.
