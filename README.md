# Threadlock

A mobile puzzle game about glowing liquid in glass pipes. Drop a few pieces into sockets (separators,
turnstiles, tanks, flippers, overflows, bridges, mixers, twin gates), tap play and watch it run. If two
colours share a junction at the same moment, they turn to mud. Every piece is an idea from concurrent
programming and every failure a real concurrency bug, but players only ever see liquid, pieces and cups.

Four worlds (60 levels + 12 challenges), a daily puzzle and an endless Lab. The design is in
[DESIGN.md](DESIGN.md).

Built with [WLWengine](https://github.com/aboelbisher/WLWEngine) (a git submodule):
```
git clone --recursive git@github.com:aboelbisher/ThreadLock.git
```
Android: open `android/` in Android Studio, or run `android\gradlew.bat assembleDebug`.
Levels live in `assets/levels/`; `tools/threadlock_tool.cpp` checks, solves and renders them (see CLAUDE.md).
