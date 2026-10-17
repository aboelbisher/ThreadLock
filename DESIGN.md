# Threadlock: game design

Draft 2 of 17 October 2026: **v3**, approved by the owner on 16 October. v3 makes the puzzles harder
(any piece in any socket) and gives every world its own look and its own idea. The illustrated version is
the claude.ai canvas "Threadlock v3: open sockets + art" (round 1: open sockets; round 2: machine parts;
round 3: four worlds, one city). Draft 1 (v2, 16 October) is in git history; §13 lists what moved.

Glowing liquid pours through a machine. You drop a few pieces into sockets, tap play, and watch it run.
If two colours share a junction at the same moment, they turn to mud. Every piece is an idea from
concurrent programming and every failure is a real concurrency bug, but players only ever see liquid,
pieces and machines. Portrait, one hand, runs of 20–60 s.

## 1. The core

**The one rule: different colours must never share a junction at the same moment.**

The play loop: **look** (spouts show every drop they will pour, cups what they want), **place** (drag
pieces from the tray into sockets, tap a piece to set it), **play** (one tap; from here on you never
touch the liquid), **watch** (near misses sparkle, cups fill with chords), **fix** ("Show me" rewinds
to the moment it went wrong), **pure** (every cup full and pure: stars, then the next machine).

| Player sees | What happened | The real bug | First met |
|---|---|---|---|
| **Mud**: two colours touch, turn brown and lose their glow | different colours inside one junction at once | race condition | World 1 |
| **Spill**: a cup overflows | more drops than the cup takes | overflow | World 2 |
| **Dry**: a cup ends short | its drops went elsewhere or never came | starvation | World 2 |
| **THREADLOCKED**: everything stops, padlocks, a ring of arrows | pieces wait for each other in a circle, or for a partner or a signal that never comes | deadlock | World 3 |
| **Muddled**: a layered glass gets its layers out of order | drops arrived out of order | ordering bug | later worlds |
| **Ink**: a cup turns black | a bad drop reached a cup | unhandled error | later worlds |

Pillars: what you see is the timing (one speed everywhere, deterministic runs); place, then watch;
failure teaches (every failure has a place and a moment); one new idea at a time; pure feels good,
mud feels bad.

## 2. How the liquid moves (rules v3)

- **Drops.** Liquid moves in drops. A drop fills one cell of pipe and moves one cell per tick (8 ticks
  a second), at the same speed everywhere: a pipe's cell count comes from its drawn length.
- **Pipes** are one-way and hold one drop per cell. A drop that can't move waits; the ones behind it
  queue up (the pipe visibly backs up).
- **Spouts** pour a known sequence on a steady beat, shown as beads by the spout. A spout whose pipe is
  backed up waits; nothing is lost.
- **Junctions.** A drop needs 3 ticks to pass a crossing or a merge. Different colours inside at the
  same time make mud. **Same colours may share it** (decision 1).
- **Forks** have a flap that sends everything one way; some flaps have a handle and can be tapped. A fork
  follows its flap unless a piece that routes (separator, flipper, overflow) sits on it (§3).
- **Nothing is created or lost**, except down a drain. A blend is two drops stuck together: a
  separator pulls them apart; a mixer turns one blue and one amber into two green.
- **Cups** want one colour and an exact number of drops. A turning rack's bottles are cups too (§4).
- **A run ends** when every drop has landed (solved if every cup is full and pure), or at once when
  nothing can ever move again (threadlocked).
- **Sockets are open** (v3): every piece goes in every socket, and sockets all look the same, so the
  board no longer tells you where each piece goes. A socket sits on a fork, a pipe (a pipe socket; two
  side by side make a twin pair), a crossing or a merge. A piece does its one job where the socket gives
  it what it needs, sits idle where it doesn't, and in a few spots is a trap (§3). While you drag a piece,
  every free socket lights.

Timing: a drop entering a junction at tick t is inside during t..t+2 and leaves at t+3; another may
enter on the tick it leaves (0 ticks to spare = a near miss, with a sparkle and a chime).

## 3. The pieces

The tray holds nine pieces (the tank in two sizes), the same brass toolkit in every world.

| Piece | World | What it does | Tap | Idea |
|---|---|---|---|---|
| Separator | 1 | sorts by colour: a blend splits into its colours, a pure drop goes to its colour's side | swap sides | branching |
| Turnstile | 1 | one drop inside at a time; its arrow picks who goes first when two arrive together | turn the arrow | mutex, priority |
| Tank | 1 | holds each drop longer: short (+4) and tall (+8), **fixed sizes** (decision 4) | — | buffer, sleep |
| Flipper | 2 | left, right, left, right, starting on the side it shows | first side | round-robin |
| Overflow N | 2 | the first N drops go its way, then everything the other way | its way | counting semaphore |
| Bridge | 3 | lifts one lane over the other: no waiting, no mud. Rare | — | don't share |
| Mixer | 4 | waits for one drop from each way in and mixes them | — | join, rendezvous |
| Twin gate | 4 | holds two ways in until both have a drop, then lets both go together (W4's "twin seals") | — | barrier |

v2's Collector (W5) and Bell (W6) are no longer pieces: batching comes back as W5's tipping bucket and
signals as W3's mill and gates, both board parts (§4). Held in reserve for later worlds: the Tint (a
transform).

Colours (every pair stays apart under protanopia and deuteranopia, OKLab ΔE ≥ 10): blue #2F7BFF,
amber #FFC53D, green #3FE0A0 (= blue + amber), rose #FF4FA3, pearl #F2F6FF (= green + rose), ink, mud
#8C5C33 (no glow). Violet is out as a liquid (the same as blue for deuteranopes). Shapes back every
colour up: blue circle, amber triangle, green diamond...

### Every piece in every socket

What a piece does depends on the socket it sits in. **Acts** = its job; **idle** = nothing (it still
counts as a piece for Lean); **trap** = it waits forever: THREADLOCKED, with the lonely piece shown.

| Piece | Fork (1 in, 2 out) | Pipe socket (1 in, 1 out) | Crossing (2 lanes) | Merge (2 in, 1 out) |
|---|---|---|---|---|
| Separator | acts: as on v2's forks | idle: blends pass whole | **new**: sorts by colour whichever lane a drop came in on; blends split to both sides; still shared | idle: one way out |
| Flipper | acts | idle | **new**: left, right, left... whatever the lane; still shared | idle |
| Overflow N | acts | idle | **new**: the first N leave by its side, the rest by the other, whatever their lane; still shared | idle |
| Turnstile | idle: a fork takes one drop at a time already; its flap still works | idle | acts: one inside at a time, the arrow picks who goes first | acts: the same |
| Bridge | idle | idle | acts: the lanes don't touch | idle: the pipes join anyway, mud possible |
| Mixer | **trap**: one pipe in, its first drop waits forever for a partner | **trap** | **new**: waits for a drop from each lane, mixes them, and each goes on along its own lane | acts: two of the mix go on |
| Twin gate | **trap** | on a twin pair: acts (holds both pipes until each has a drop, then both go); on a single pipe: **trap** | **new**: holds both lanes until each has a drop, then lets both in together: two colours make mud | **new**: the same; both enter together, so they must be one colour |
| Tank +4 / +8 | **new**: holds each drop, then the flap sends it on | acts: holds each drop, one at a time | **new**: drops stay 4 or 8 ticks longer inside: a slower crossing, still shared, more time to collide | **new**: the same, on a merge |

- **Mud, exact.** Crossings and merges are shared places: two colours inside at once make mud, unless a
  turnstile or a mixer sits there, or a bridge on a crossing. Any other piece leaves them shared.
- **Flaps.** A fork follows its flap unless a separator, flipper or overflow sits on it. A tank or a
  turnstile there leaves the flap in charge, and a handled flap stays tappable.
- **Idle pieces.** No hint while you build. During a run a piece with nothing to do sits dim and still,
  so the replay shows why the run failed. The long-press card lists where each piece works.
- **Traps** end the run as THREADLOCKED with the lonely piece reported (World 4's flavour: "a lonely
  mixer cracks its seal").

Details settled while building rules v3 (17 October; not on the canvas):
- A separator on a crossing sends its colour a out by its side and colour b by the other; other colours
  keep their lane; an a+b blend splits as on a fork (both ways out must be free).
- A mixer on a crossing: each mixed drop leaves along the lane it came in on.
- A twin gate dropped on either half of a twin pair takes both halves (as in v2). Another piece on one
  half acts as on a single pipe socket.
- A twin gate on a crossing or merge lets the front drops of both lanes in on the same tick, never one
  alone; the next pair may follow at once.

## 4. Machine parts

Board parts are placed by the level, not the player: spouts (pure or blend; fast or slow beat), forks
with flaps (some with handles), crossings, merges, pipe sockets and twin pairs, cups, drains (W2's sink)
and locked pieces (repair levels). v3 adds machines that move and act on each other, each one a
concurrency idea:

- **Turning rack** (W2, time slicing): a turntable of K bottles under one tap that steps round on a
  clock ("the rack turns every 4 ticks"). A drop lands in whichever bottle is under the tap when it
  arrives, so tanks pick the slice. Each bottle is a cup of its own (colour, count, the order's name):
  spill, dry and wrong colour count per bottle. Exact: a drop arriving at tick t lands in bottle
  (start + t / period) mod K.
- **Mill wheel and gates** (W3, signals): each drop that passes a mill turns it a notch, and its gears
  flip every gate they reach (open ↔ shut). One stream opens or shuts another's way. Two mills geared to
  each other's gates can THREADLOCK ("Two Mills"). A shut gate shows a red lamp; drops queue behind it.
  Exact: a mill sits on a pipe (1 in, 1 out), one drop at a time, 3 ticks inside like a junction; a drop
  leaving it flips each gate in its gear list once. Flips are collected during a tick and applied at its
  end, so the order of moves inside a tick doesn't matter. A gate sits on a pipe and starts open or shut;
  shut, it admits nothing (a drop already inside still leaves). One drop = one flip (no gear ratios yet).
  Mills and gates are not sockets.
- **Tipping bucket N** (W5, later, batching): fills with N drops, tips, and pours them out as one train.
  v2's Collector, built as a machine. Not in the launch build.
- **Grand machines** (finales): each world's finale is two screens tall (`"height": 1200` in its level).
  The screen shows one normal board's worth (600 units) at the usual scale, so pieces keep their size; the
  rest scrolls. Building: drag the empty board (it flings), or hold a dragged piece near the view's top or
  bottom edge. Opening: a pan from the bottom to the top (~2 s; a touch skips it; none with reduced
  motion). Running: the view rides down with the liquid's front and never climbs back; it goes to the
  first failure, and Show me starts there; a finger takes over for 2.5 s. Solved: the view centres the
  payoff (W1: the rose window, `"window"`) while it lights, and the card comes after 3 s.

Level files also gain look-only extras the rules ignore, added with each world's renderer: names (orders,
districts), decor, a style per pipe (tube / coil / drip; copper; canal / aqueduct; groove, curved in W4)
and a finale's height.

## 5. The city and its worlds

**One city that runs on liquid light.** Each world is one of its trades: its own place, its own way of
building a machine, its own goal, its own new parts and its own payoff. Each world has 15 levels on the
main path (intro, explorations, a breather around 8, a repair, a hard stretch; the finale is the last
one) plus 3 optional challenges. Every level is a named machine with its own silhouette and its own
small idea; worlds and levels must not look the same or share one base idea.

**The same everywhere:** the tray's pieces (one brass toolkit, the same emblem in every world), the liquid
colours and their shapes, the rules, the HUD buttons and the result cards (tinted per world, same layout:
back, title + "n-m · world", three stars with their pars, hint / restart / speed / the big EDIT button). A
piece learnt in World 1 reads the same in World 4.

**The map is the city:** one illustrated map with four districts (the tower, the market square, the canal
quarter, the sanctum). Solving a level lights its corner of its district, so progress shows as the city
waking up.

### World 1 · First Light · The Lantern Works
- **Place:** the glassworks at the top of the tower, at dawn.
- **Board:** a cascade seen from the side: light pours from crucibles at the top and falls tier by tier,
  through clear glass tubes (brass collars, round bends), coils (count the turns to read the delay) and
  drip falls (drops fall through the air into a funnel; the gap is the timing). Glass globes sit at
  crossings and merges, where you watch the lanes pass and see the mud.
- **From → to:** hanging crucibles → lanterns that glow as they fill. **New:** separator, turnstile,
  tanks; mud.
- **Payoff:** each lit lantern lights a pane of the great rose window; the world's map is the window
  filling pane by pane. **Fail:** mud chokes the light into grey smoke.
- **Sound:** glass harmonica, celesta, chimes. **Finale:** 1-15 The Rose Window, two screens: three
  crucibles (pure blue, a blend, alternating) and a knot at the top; on the second screen the streams cross
  in front of the window and drip into three lanterns, whose panes light it. Lean 3 (separator, tall tank on
  the knot's loop, turnstile in front of the window), Swift 4; the trap is 1-12's habit, a turnstile on the
  knot.
- **Palette:** night blue #19263B, brass #C9974E, glass #E4F0FF; background #080C15, accent #F6DA9A,
  EDIT #FFE6A6 → #DFA74E.
- **Level shapes:** The Fountain (one crucible, four lanterns: jets that must not meet); The Hourglass
  (two streams squeeze through one waist); The Chandelier (out along the arms and back to one hub; drawn
  as 1-6, "Light both lanterns. The globe is shared.", ≤ 2 pieces, ≤ 0:18); The Braid (three strands,
  three crossings, one order).

### World 2 · Fair Shares · The Apothecary
- **Place:** an apothecary on market day.
- **Board:** the shop wall seen from the front: copper lines run along the shelves and down to the
  counter, with a porthole for every tick and brass taps.
- **From → to:** stoneware jars → bottles, each for a named order on the cork board (handwritten slips).
  **New:** flipper, overflow, the sink (the drain), the turning rack; spill and dry.
- **Payoff:** each filled bottle is corked and labelled; the shop bell rings and its slip is stamped
  paid. **Fail:** a spill puddles on the counter; a short bottle leaves its order unpaid.
- **Sound:** marimba, pizzicato strings, the shop bell. **Finale:** Market Day, the whole shop.
- **Palette:** dark wood #3D2C1F, copper #C86E3A, parchment #F3E8CC, #8A6038; background #1A120C, accent
  #FFB45C, EDIT #FFD99A → #EE9645; slips in the Caveat font.
- **Level shapes:** Morning Orders (halve, then halve again, for three customers; also drawn as 2-7 with
  an overflow 5 to a tap over a 3-bottle rack and the rest to the sink, slips "I Mrs Hale 4", "II Baker
  2", "III Dr Moss 2", "Three orders, one tap. The rack turns every 4 ticks.", ≤ 1 piece, ≤ 0:20; one
  version is picked when W2 is built); The Lazy Susan (one tap, a turning rack: the timing picks the
  bottle); Inventory (the first few to the bottle, the rest down the sink); The Back Room (two jars on
  two shelves; their lines cross on the way down).

### World 3 · Rush Hour · The Canal City
- **Place:** the city's canals at night, seen from above.
- **Board:** a map: canals run between rooftops and traffic goes any way the canals go, through locks
  and aqueducts (bridges).
- **From → to:** locks letting barges of light out on fast and slow beats → harbours that light their
  districts. **New:** bridges, merges, mills geared to gates; the first THREADLOCKED.
- **Payoff:** each supplied district lights its streets; the world ends with the whole city lit.
  **Fail:** barges collide into murk; THREADLOCKED is gridlock, red lamps all round.
- **Sound:** late-night jazz, double bass, harbour horns. **Finale:** Rush Hour, the whole city.
- **Palette:** night #0B111D, canal #0A2230, slate roofs #46526C, lamp #FFC46A, red lamp #FF5A4A; accent
  #7FE3D8, EDIT #A8F2EA → #2FB5A8; street and district names in Cormorant SC.
- **Level shapes:** Old Bridge (two canals cross once: bridge it or time it); The Roundabout (a ring
  where traffic can meet itself coming round); Two Mills (each mill opens the other's gate: who goes
  first?); Harbour Mouth (three canals funnel into one harbour). Drawn: 3-6 The Mill Gate, "Each amber
  barge turns the mill. The mill flips the blue gate.", districts Lantern Row and Old Quay, ≤ 2 pieces,
  ≤ 0:30.

### World 4 · The Mixing Hall · The Sanctum
- **Place:** the alchemists' sanctum, an obsidian table lit by candles.
- **Board:** a sigil seen from above, circles and figures inlaid in gold: ingredients enter at the rim and
  meet at its points, through gold-lipped grooves (curved) with glass globes at the points.
- **From → to:** element flasks → phials for the elixir. **New:** mixer, twin seals (the twin gate), rose
  and pearl; a mixer whose partner never comes.
- **Payoff:** the sigil ignites line by line and the elixir rises from the phial. **Fail:** a bad mix
  bubbles into sludge; a lonely mixer cracks its seal.
- **Sound:** choir, harp, organ drones. **Finale:** The Great Work.
- **Palette:** obsidian #100C17, gold #C9A85A, pale #EDE4FF, green #3FE0A0, violet #8E6CD8 (decor only:
  violet is never a liquid); accent #E6CC88, EDIT #DCC8FF → #9A6CF2.
- **Level shapes:** First Green (blue meets amber, and here that's the point); The Triangle (three
  points, three joins, three colours); Twin Seals (two grooves, one seal: they arrive together); Hexagram
  (six points, two triangles, pearl at the heart). Drawn: 4-7 The Twin Seals, "Both seals open together,
  or not at all.", ≤ 2 pieces, ≤ 0:16.

### Worlds 5–8
Live updates after launch, to be re-planned (v2's plan: The Layer Bar, Signal Tower, Clockworks, The
Grand Works). Signals and time slices have moved into worlds 3 and 2; W5 keeps batching (the tipping
bucket).

Level types: build (default), repair (pieces placed, one thing wrong), spectacle (a breather made for
watching), challenge (optional, hard, unlocks cosmetics), daily, Lab.

## 6. Stars

- **Solve**: every cup full and pure.
- **Lean**: no more than the par number of pieces.
- **Swift**: within the par time.

Stars stick once earned, from any run; Lean and Swift in one run add a gold frame. Pars come from the
solver (fewest pieces; fastest run). Locks are safe but slow, timing is fast but fragile, so a level
often has two good answers. World gates: the next world opens at about 45% of the stars before it
(World 2 at 20, World 3 at 40, World 4 at 60).

## 7. Why players stay

The run (20–60 s of tension and music), the level (place, run, fix; instant retry), the session (a
world's arc), the days (the daily puzzle and its streak, challenges, the Lab, collections,
achievements). First launch: no menu, level 1-1 on screen at once; the map appears after 1-3.
Daily puzzle: generated, the same for everyone, easy on Monday and hard on Sunday; a streak; the last
week's archive. The Lab: endless generated puzzles at a chosen difficulty, after World 2. Collections:
liquid palettes earned with challenges and streaks (a high-contrast palette is free). Achievements:
Threadlocked, Close Shave (100 near misses), Lean Machine, Two Answers. No energy, no lives, no timers
on thinking, no paying to progress.

## 8. Screens

Level: the HUD on top (back, the level's name and "n-m · world", three stars with their pars); the whole
machine in the middle (no panning, except a finale's two screens, §4 Grand machines: a thin rail on the
right shows where the view is, the board fades out under the top bar and above the tray, and a chevron
says "more below" until the first scroll); the tray, hint, restart, speed (1×, 2×, 4×) and the big
EDIT / play button at the bottom. Drag from the tray (every free socket lights), tap a placed piece to
set it, drag it off to send it back, long-press for its card (what it does and where it works). During a
run an idle piece sits dim and still. A solved card (stars, time, pieces; Next, Replay) or a failed card
(what went wrong and where; Show me, Edit), tinted per world. **Show me** replays a second before the
failure at quarter speed under a spotlight. Hints in three steps: Show me (free), place one (1 hint),
show the solution (2 hints or a rewarded ad; Lean and Swift then need a machine of your own).
Map: the city (§5); worlds open behind star gates.

## 9. Look, sound and feel

Each world has its own materials, palette and instruments (§5); what you carry and the liquid look the
same everywhere. Metaball liquid with bloom; mud has no glow. The machine plays music: each colour an
instrument, a landing note climbs as its cup fills, a full cup rings a chord, a tick is a sixteenth at
120 BPM; mud is a dull thud, a jam a grinding stop and a low organ chord. Near misses sparkle and chime.
Haptics: a light tap per landing, a heavy one for mud and jams. Accessibility: shapes for colours, a
high-contrast palette, reduced motion, large touch targets, one hand.

## 10. Ads (as in Bluzzle)

No banners, no app-open ads, nothing while editing or running. Interstitials only after a solve, on
Next or Map, paced by `kit::AdPacing {first_after_games = 10, games_between = 3, min_seconds_between =
180}` (a solved level counts as a game) and never after a failed run, a world finale, the daily puzzle
or a solve that took five or more runs. Rewarded: +1 hint (up to 5 a day), show the solution, a missed
daily (once a week it also mends the streak), a Lab hint. Later: remove-ads purchase (needs Play
Billing in the kit), cosmetics never sold for ads.

## 11. Building it

v2 was built on 16 October: rules v2, solver, generator, level tool, 72 levels, view v2, map, daily, Lab,
saves, sounds, ads. v3, in order; each step is built, verified (PC tests and the Pixel 4), shown, and
committed after the owner's OK, in small commits:

0. This document.
1. Rules v3 with tests: open sockets, the mud / flap / idle rules, traps, the turning rack, mills and
   gates, and their level JSON. The solver, the level tool and the generator follow; every par is
   re-baked.
2. World 1 complete, as the vertical slice: the Lantern Works renderer; the brass toolkit and the HUD;
   its 15 levels + 3 challenges, redesigned as named machines in the cascade grammar; The Rose Window
   finale (two screens, scroll + a camera that rides down with the liquid); its sounds; the city map with
   the tower district.
3. Worlds 2, 3 and 4, one at a time, the same way.
4. Playtest World 1 with 5–10 people; adjust levels with the tool.
5. Launch: Threadlock's own AdMob app and units, analytics, store page, closed test. Then worlds 5–8 as
   live updates.

The cost the owner accepted: four looks are about four times the drawing work (each world needs its own
pipes, containers, junctions, background, payoff animation and sounds), and the level files gain
per-world extras. To decide when we get there: which look the daily and the Lab use; the W5–8 re-plan.

## 12. Decisions

Taken 16 October 2026 (v2), all as recommended:
1. Same colours may share a junction: **yes**.
2. An empty fork follows its flap: **yes** (v3: a fork follows its flap unless a router sits on it).
3. Pieces fit socket shapes: **replaced by v3: every piece fits every socket.**
4. Tanks come in fixed sizes (+4, +8): **yes**.
5. The stars are Solve / Lean / Swift: **yes**.
6. Launch with worlds 1–4, the daily and the Lab: **yes**.

Taken 16 October 2026 (v3, round 3 of the canvas), all as recommended:
7. Any piece in any socket, as in the table of §3.
8. Mills geared to gates in World 3; the turning rack in World 2; the tipping bucket later (World 5).
9. Finales two screens tall.
10. One city as the map.
11. Rules first, then a World 1 vertical slice.

## 13. What moved from v2

- Sockets: shaped (decision 3) → open: any piece in any socket, idle or a trap where it has nothing to do.
- Mud: any piece in a junction prevented it → only a turnstile, a mixer or a bridge (on a crossing) does.
- Signals: W6's bell + symbol gates → W3's mill wheels geared to gates (board parts, not pieces).
- Time slices: W7's clock gates → W2's turning rack.
- Pearl: W7 → W4.
- Batching: W5's Collector piece → W5's tipping bucket (a board part).
- Looks: one dark-glass look for every world → a look per world, one city as the map.
- World names stay, and every world still has 15 main-path levels plus 3 challenges; the levels are
  redesigned as named machines when each world is built. Worlds 5–8 need re-planning.
