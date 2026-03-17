#!/bin/sh
#
# Use this script to run your program LOCALLY.

set -e # Exit early if any commands fail

# Build the project locally.
(
  cd "$(dirname "$0")" # Ensure compile steps are run within the repository directory
  PROJECT_DIR="$(pwd)"

  TOOLCHAIN_ARG=""
  if [ -n "$VCPKG_ROOT" ] && [ -f "$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" ]; then
    TOOLCHAIN_ARG="-DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
  elif [ -f build/CMakeCache.txt ] && grep -q "CMAKE_TOOLCHAIN_FILE:.*=/scripts/buildsystems/vcpkg.cmake" build/CMakeCache.txt; then
    # Drop stale cache created when VCPKG_ROOT was empty.
    rm -rf build
  fi

  if [ -f build/CMakeCache.txt ] &&
     grep -q "CMAKE_HOME_DIRECTORY:INTERNAL=$PROJECT_DIR" build/CMakeCache.txt; then
    :
  elif [ -f build/CMakeCache.txt ]; then
    # Directory rename/move can make CMakeCache.txt unusable.
    rm -rf build
  fi

  cmake -B build -S . $TOOLCHAIN_ARG
  cmake --build ./build
)

# Run the locally built shell.
exec $(dirname "$0")/build/shell "$@"
