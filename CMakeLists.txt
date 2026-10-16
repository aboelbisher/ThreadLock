cmake_minimum_required(VERSION 3.21)

project(Threadlock VERSION 0.2.0 LANGUAGES C CXX)

# ---- Engine (git submodule) ----
set(WLW_ENGINE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/WLWengine")
if(NOT EXISTS "${WLW_ENGINE_DIR}/CMakeLists.txt")
  message(FATAL_ERROR "Engine submodule missing. Run: git submodule update --init --recursive")
endif()
add_subdirectory("${WLW_ENGINE_DIR}" wlwengine)

# ---- The rules: plain C++ (no engine), shared by the game, the tests and the level tool ----
set(THREADLOCK_RULES
  src/threadlock_rules.cpp
  src/threadlock_rules.h
  src/level_io.cpp
  src/level_io.h
  src/solver.cpp
  src/solver.h
  src/generator.cpp
  src/generator.h)

# ---- Game ----
# One target for every platform: an .exe on desktop (the engine's dev target), libthreadlock.so on Android.
wlw_add_game(threadlock
  SOURCES
    src/main.cpp
    src/threadlock_app.cpp
    src/threadlock_app.h
    src/audio_synth.cpp
    src/audio_synth.h
    src/board_view.cpp
    src/board_view.h
    src/catalog.cpp
    src/catalog.h
    src/feedback.cpp
    src/feedback.h
    src/level_screen.cpp
    src/level_screen.h
    src/liquid.cpp
    src/liquid.h
    src/map_screen.cpp
    src/map_screen.h
    src/menus.cpp
    src/menus.h
    src/palette.h
    src/progress.cpp
    src/progress.h
    src/ui_parts.h
    ${THREADLOCK_RULES}
  ASSETS
    "${CMAKE_CURRENT_SOURCE_DIR}/assets")
target_link_libraries(threadlock PRIVATE wlw::kit wlw_json)

# The rules run thousands of simulations (the solver, the daily puzzle's generator) and the sounds are
# synthesized at startup: optimized even in Debug builds, like the engine (WLW_OPTIMIZE_ENGINE_IN_DEBUG).
# Step through them on the PC instead.
if(NOT MSVC)
  set_source_files_properties(src/threadlock_rules.cpp src/solver.cpp src/generator.cpp src/level_io.cpp src/audio_synth.cpp
    TARGET_DIRECTORY threadlock PROPERTIES COMPILE_OPTIONS "$<$<CONFIG:Debug>:-O2>")
endif()

set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT threadlock)
get_target_property(_threadlock_sources threadlock SOURCES)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${_threadlock_sources})

# ---- Tests and the level tool (desktop only) ----
# Run with: ctest --test-dir build/<preset>. Without CMake (no vcpkg needed), see CLAUDE.md.
if(NOT ANDROID)
  option(THREADLOCK_BUILD_TESTS "Build the unit tests and the level tool" ON)
  if(THREADLOCK_BUILD_TESTS)
    enable_testing()
    add_executable(threadlock_tests
      tests/test_main.cpp
      tests/threadlock_rules_tests.cpp
      tests/solver_tests.cpp
      tests/content_tests.cpp
      tests/test_levels.h
      ${THREADLOCK_RULES})
    target_compile_features(threadlock_tests PRIVATE cxx_std_20)
    target_link_libraries(threadlock_tests PRIVATE wlw_json wlw_doctest)
    target_compile_definitions(threadlock_tests PRIVATE THREADLOCK_ASSETS="${CMAKE_CURRENT_SOURCE_DIR}/assets")
    add_test(NAME threadlock_tests COMMAND threadlock_tests)

    add_executable(threadlock_tool tools/threadlock_tool.cpp ${THREADLOCK_RULES})
    target_compile_features(threadlock_tool PRIVATE cxx_std_20)
    target_link_libraries(threadlock_tool PRIVATE wlw_json)
    target_include_directories(threadlock_tool SYSTEM PRIVATE "${WLW_ENGINE_DIR}/third_party/stb")
  endif()
endif()
