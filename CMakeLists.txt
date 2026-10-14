cmake_minimum_required(VERSION 3.21)

project(Threadlock VERSION 0.1.0 LANGUAGES C CXX)

# ---- Engine (git submodule) ----
set(WLW_ENGINE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/WLWengine")
if(NOT EXISTS "${WLW_ENGINE_DIR}/CMakeLists.txt")
  message(FATAL_ERROR "Engine submodule missing. Run: git submodule update --init --recursive")
endif()
add_subdirectory("${WLW_ENGINE_DIR}" wlwengine)

# ---- Game ----
# One target for every platform: an .exe on desktop (the engine's dev target), libthreadlock.so on Android.
wlw_add_game(threadlock
  SOURCES
    src/main.cpp
    src/threadlock_app.cpp
    src/threadlock_app.h
    src/threadlock_rules.cpp
    src/threadlock_rules.h
  ASSETS
    "${CMAKE_CURRENT_SOURCE_DIR}/assets")

set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT threadlock)
get_target_property(_threadlock_sources threadlock SOURCES)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${_threadlock_sources})

# ---- Tests (desktop only) ----
# The rules are plain C++ (no engine), so they're tested on their own.
# Run with: ctest --test-dir build/<preset>
if(NOT ANDROID)
  option(THREADLOCK_BUILD_TESTS "Build the unit tests" ON)
  if(THREADLOCK_BUILD_TESTS)
    enable_testing()
    add_executable(threadlock_rules_tests tests/threadlock_rules_tests.cpp src/threadlock_rules.cpp)
    target_compile_features(threadlock_rules_tests PRIVATE cxx_std_20)
    add_test(NAME threadlock_rules_tests COMMAND threadlock_rules_tests)
  endif()
endif()
