#!/bin/sh
#
# Use this script to run your program LOCALLY.
#
# Note: Changing this script WILL NOT affect how CodeCrafters runs your program.
#
# Learn more: https://codecrafters.io/program-interface

set -e # Exit early if any commands fail

# Copied from .codecrafters/compile.sh
#
# - Edit this to change how your program compiles locally
# - Edit .codecrafters/compile.sh to change how your program compiles remotely
(
  cd "$(dirname "$0")" # Ensure compile steps are run within the repository directory

  TOOLCHAIN_ARG=""
  if [ -n "$VCPKG_ROOT" ] && [ -f "$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" ]; then
    TOOLCHAIN_ARG="-DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
  elif [ -f build/CMakeCache.txt ] && grep -q "CMAKE_TOOLCHAIN_FILE:.*=/scripts/buildsystems/vcpkg.cmake" build/CMakeCache.txt; then
    # Drop stale cache created when VCPKG_ROOT was empty.
    rm -rf build
  fi

  cmake -B build -S . $TOOLCHAIN_ARG
  cmake --build ./build
)

# Copied from .codecrafters/run.sh
#
# - Edit this to change how your program runs locally
# - Edit .codecrafters/run.sh to change how your program runs remotely
exec $(dirname "$0")/build/shell "$@"
