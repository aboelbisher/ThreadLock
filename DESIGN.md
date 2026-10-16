# Threadlock: game design

Draft 1 of 16 October 2026, with the owner's six decisions taken (all as recommended, §13). The
illustrated version (nine puzzles drawn as phone screens) is the claude.ai artifact this file came from.

Glowing liquid pours through glass pipes. You drop a few pieces into sockets, tap play, and watch it
run. If two colours share a junction at the same moment, they turn to mud. Every piece is an idea
from concurrent programming and every failure is a real concurrency bug, but players only ever see
liquid, pieces and cups. Portrait, one hand, runs of 20–60 s.

## 1. The core

**The one rule: different colours must never share a junction at the same moment.**

The play loop: **look** (spouts show every drop they will pour, cups what they want), **place** (drag
pieces from the tray into sockets, tap a piece to set it), **play** (one tap; from here on you never
touch the liquid), **watch** (near misses sparkle, cups fill with chords), **fix** ("Show me" rewinds
to the moment it went wrong), **pure** (every cup full and pure: stars, then the next board).

| Player sees | What happened | The real bug | First met |
|---|---|---|---|
| **Mud**: two colours touch, turn brown and lose their glow | different colours inside one junction at once | race condition | World 1 |
| **Spill**: a cup overflows | more drops than the cup takes | overflow | World 2 |
| **Dry**: a cup ends short | its drops went elsewhere or never came | starvation | World 2 |
| **THREADLOCKED**: everything stops, padlocks, a ring of arrows | pieces wait for each other in a circle, or for a partner that never comes | deadlock | World 3 |
| **Muddled**: a layered glass gets its layers out of order | drops arrived out of order | ordering bug | World 5 |
| **Ink**: a cup turns black | a bad drop reached a cup | unhandled error | World 6 |

Pillars: what you see is the timing (one speed everywhere, deterministic runs); place, then watch;
failure teaches (every failure has a place and a moment); one new idea at a time; pure feels good,
mud feels bad.

## 2. How the liquid moves (rules v2)

- **Drops.** Liquid moves in drops. A drop fills one cell of pipe and moves one cell per tick (8 ticks
  a second), at the same speed everywhere: a pipe's cell count comes from its drawn length.
- **Pipes** are one-way and hold one drop per cell. A drop that can't move waits; the ones behind it
  queue up (the pipe visibly backs up).
- **Spouts** pour a known sequence on a steady beat, shown as beads by the spout. A spout whose pipe is
  backed up waits; nothing is lost.
- **Junctions.** A drop needs 3 ticks to pass a crossing or a merge. Different colours inside at the
  same time make mud. **Same colours may share it** (decision 1).
- **Forks** have a flap that sends everything one way (**an empty fork follows its flap**, decision 2);
  some flaps have a handle and can be tapped. A piece in the fork's socket replaces the flap.
- **Nothing is created or lost**, except down a drain. A blend is two drops stuck together: a
  separator pulls them apart; a mixer turns one blue and one amber into two green.
- **Cups** want one colour and an exact number of drops. Layered glasses want a sequence (World 5).
- **A run ends** when every drop has landed (solved if every cup is full and pure), or at once when
  nothing can ever move again (threadlocked).
- **Sockets have shapes** (decision 3): fork sockets take routing pieces, junction sockets take locks,
  bridges and mixers, pipe sockets take tanks (later collectors and bells), twin sockets twin gates.
  While you drag a piece, the sockets it fits light up.

Timing: a drop entering a junction at tick t is inside during t..t+2 and leaves at t+3; another may
enter on the tick it leaves (0 ticks to spare = a near miss, with a sparkle and a chime).

## 3. The pieces

| Piece | World | What it does | Tap | Fits | Idea |
|---|---|---|---|---|---|
| Separator | 1 | splits a blend into its colours; a pure drop goes to its colour's side | swap sides | forks | branching |
| Turnstile | 1 | one drop at a time; its arrow picks who goes first when two arrive together | turn the arrow | crossings, merges | mutex, priority |
| Tank | 1 | holds each drop its ticks, one at a time; short (+4) and tall (+8), **fixed sizes** (decision 4) | — | pipes | buffer, sleep |
| Flipper | 2 | left, right, left, right, starting on the side it shows | first side | forks | round-robin |
| Overflow N | 2 | the first N drops go its way, then everything the other way | its way | forks | counting semaphore |
| Bridge | 3 | lifts one lane over the other: no waiting, no mud. Rare | — | crossings | don't share |
| Mixer | 4 | waits for one drop from each pipe, sends out two of the mix | — | merges | join, rendezvous |
| Twin gate | 4 | holds two side-by-side pipes until both have a drop, then both go | — | twin sockets | barrier |
| Collector N | 5 | collects N drops, lets them go as one train | — | pipes | batching |
| Bell | 6 | rings as a drop passes; gates with its symbol flip | symbol | pipes | signal |

Held in reserve for World 7: the Tint (a transform). Board parts placed by levels: spouts (pure,
blend, striped, ink; fast or slow beat), crossings, merges, forks with flaps, cups, layered glasses,
drains, gates ♦♥♣, clock gates, rings, locked pieces (repair levels).

Colours (every pair stays apart under protanopia and deuteranopia, OKLab ΔE ≥ 10): blue #2F7BFF,
amber #FFC53D, green #3FE0A0 (= blue + amber), rose #FF4FA3, pearl #F2F6FF (= green + rose), ink, mud
#8C5C33 (no glow). Violet is out (the same as blue for deuteranopes). Shapes back every colour up.

## 4. Worlds (launch: 1–4, decision 6)

Each world: 15 levels on the main path (intro, explorations, a breather around 8, a repair, a hard
stretch, a finale) plus 3 optional challenges.

1. **First Light** (races): separator, turnstile, tanks; mud. Two Cups is 1-4; Double Cross, Loop-the-Loop,
   Who Goes First, Fix It, Choose Wisely, Braid, Rush, The Weave. Challenges: Needle, Two Loops, No Locks.
2. **Fair Shares** (counting): flipper, overflow, drains; spill and dry. Three Ways, Odd One Out, Waste
   Not, Sorted Shares, Fair Shares. Challenges: Primes, Lopsided, Exact Change.
3. **Rush Hour** (traffic): bridge, merges, fast and slow spouts; the first THREADLOCKED (3-8 Two Locks).
   Shared Pipe, Polyrhythm, Back-Up, Three Locks, Rush Hour. Challenges: Gridlock, Zipper, Crossroads.
4. **The Mixing Hall** (joins): mixer, twin gate, rose and green; a mixer whose partner never comes.
   Green, Unmix Remix, Lonely Blue, Twins, Palette, Hold and Wait, The Dye Works. Challenges: Hold and
   Wait II, Barrier, Leftover Rose.
5. The Layer Bar (order) · 6. Signal Tower (signals, ink) · 7. Clockworks (time, rings, pearl) ·
8. The Grand Works (everything): live updates after launch.

Level types: build (default), repair (pieces placed, one thing wrong), spectacle (a breather made for
watching), challenge (optional, hard, unlocks cosmetics), daily, Lab.

## 5. Stars (decision 5)

- **Solve**: every cup full and pure.
- **Lean**: no more than the par number of pieces.
- **Swift**: within the par time.

Stars stick once earned, from any run; Lean and Swift in one run add a gold frame. Pars come from the
solver (fewest pieces; fastest run). Locks are safe but slow, timing is fast but fragile, so a level
often has two good answers. World gates: the next world opens at about 45% of the stars before it
(World 2 at 20, World 3 at 40, World 4 at 60).

## 6. Why players stay

The run (20–60 s of tension and music), the level (place, run, fix; instant retry), the session (a
world's arc), the days (the daily puzzle and its streak, challenges, the Lab, collections,
achievements). First launch: no menu, level 1-1 on screen at once; the map appears after 1-3.
Daily puzzle: generated, the same for everyone, easy on Monday and hard on Sunday; a streak; the last
week's archive. The Lab: endless generated puzzles at a chosen difficulty, after World 2. Collections:
liquid palettes earned with challenges and streaks (a high-contrast palette is free). Achievements:
Threadlocked, Close Shave (100 near misses), Lean Machine, Two Answers. No energy, no lives, no timers
on thinking, no paying to progress.

## 7. Screens

Level: name and star targets on top; the whole board in the middle (no panning); the tray, play/stop,
speed (1×, 2×, 4×), hint and restart at the bottom. Drag from the tray, tap a placed piece to set it,
drag it off to send it back, long-press for its card. A solved card (stars, time, pieces; Next, Replay)
or a failed card (what went wrong and where; Show me, Edit). **Show me** replays a second before the
failure at quarter speed under a spotlight. Hints in three steps: Show me (free), place one (1 hint),
show the solution (2 hints or a rewarded ad; Lean and Swift then need a machine of your own).
Map: one vertical pipe network; solving pours light into the next level; worlds behind star valves.

## 8. Look, sound and feel

Dark glass pipes, metaball liquid, bloom; mud has no glow. The machine plays music: each colour an
instrument, a landing note climbs as its cup fills, a full cup rings a chord, a tick is a sixteenth at
120 BPM; mud is a dull thud, a jam a grinding stop and a low organ chord. Near misses sparkle and chime.
Haptics: a light tap per landing, a heavy one for mud and jams. Accessibility: shapes for colours, a
high-contrast palette, reduced motion, large touch targets, one hand.

## 9. Ads (as in Bluzzle)

No banners, no app-open ads, nothing while editing or running. Interstitials only after a solve, on
Next or Map, paced by `kit::AdPacing {first_after_games = 10, games_between = 3, min_seconds_between =
180}` (a solved level counts as a game) and never after a failed run, a world finale, the daily puzzle
or a solve that took five or more runs. Rewarded: +1 hint (up to 5 a day), show the solution, a missed
daily (once a week it also mends the streak), a Lab hint. Later: remove-ads purchase (needs Play
Billing in the kit), cosmetics never sold for ads.

## 10. Building it

1. Rules v2 (plain C++, tests, levels as JSON) · 2. Tools (solver, generator, level tool) · 3. View v2 ·
4. World 1 vertical slice + playtest · 5. Meta (map, saves, hints) · 6. Worlds 2–4, daily, Lab ·
7. Launch (ads, analytics, store, closed test) · 8. Live worlds 5–8.

## 13. Decisions (taken 16 October 2026)

1. Same colours may share a junction: **yes**.
2. An empty fork follows its flap: **yes**.
3. Pieces fit socket shapes: **yes**.
4. Tanks come in fixed sizes (+4, +8): **yes**.
5. The stars are Solve / Lean / Swift: **yes**.
6. Launch with worlds 1–4, the daily and the Lab: **yes**.
