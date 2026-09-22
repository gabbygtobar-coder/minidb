cmake_minimum_required(VERSION 3.16)

project(MiniDB
  VERSION 0.1.0
  DESCRIPTION "From-scratch relational database engine"
  LANGUAGES CXX
)

# M0 is a skeleton. The user-facing banner reports 0.1 (see include/minidb/version.hpp).
# The CMake project version stays 0.1.0 so packaging tools see a full version triple.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

function(minidb_set_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /WX)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
endfunction()

add_library(minidb_core STATIC
  src/cli/repl.cpp
)
target_include_directories(minidb_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_features(minidb_core PUBLIC cxx_std_17)
minidb_set_warnings(minidb_core)

add_executable(minidb
  src/main.cpp
)
target_link_libraries(minidb PRIVATE minidb_core)
minidb_set_warnings(minidb)

include(CTest)

if(BUILD_TESTING)
  # Extracted archives get a reproducible timestamp (CMake >= 3.24).
  if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
  endif()

  # GoogleTest is downloaded at configure time with FetchContent.
  # A git submodule is intentionally not used: the pin lives next to the
  # targets that link it, CI does not need recursive clone, and the
  # dependency is not copied into this repository's history.
  # Release: https://github.com/google/googletest/releases/tag/v1.15.2
  include(FetchContent)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/releases/download/v1.15.2/googletest-1.15.2.tar.gz
    URL_HASH SHA256=7b42b4d6ed48810c5362c265a17faebe90dc2373c885e5216439d37927f02926
  )
  FetchContent_MakeAvailable(googletest)

  add_executable(minidb_tests
    tests/version_test.cpp
    tests/repl_test.cpp
  )
  target_link_libraries(minidb_tests PRIVATE minidb_core GTest::gtest_main)
  minidb_set_warnings(minidb_tests)

  include(GoogleTest)
  gtest_discover_tests(minidb_tests)
endif()
