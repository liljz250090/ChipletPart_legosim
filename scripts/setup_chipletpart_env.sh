#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: scripts/setup_chipletpart_env.sh [options]

Check the current shell environment, initialize submodules, and optionally build
ChipletPart_legosim. This script does not create environments or install
packages.

Options:
  --build           Configure and build the project.
  --clean-build     Remove the build directory before configuring.
  --jobs N          Build parallelism for cmake --build. Default: nproc or 4.
  --skip-checks     Skip command/compiler checks and run configure/build steps.
  --help            Show this help.

Environment variables:
  BUILD_DIR              Build directory. Default: build
  CMAKE_BUILD_TYPE       CMake build type. Default: Release
  CXX                    C++ compiler used by CMake, if set.

Examples:
  ./scripts/setup_chipletpart_env.sh --build
  ./scripts/setup_chipletpart_env.sh --clean-build --build --jobs 8
  BUILD_DIR=build-debug CMAKE_BUILD_TYPE=Debug ./scripts/setup_chipletpart_env.sh --build
EOF
}

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

BUILD_DIR="${BUILD_DIR:-build}"
CMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE:-Release}"
BUILD_PROJECT=0
CLEAN_BUILD=0
SKIP_CHECKS=0
JOBS="${JOBS:-}"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --build)
      BUILD_PROJECT=1
      shift
      ;;
    --clean-build)
      CLEAN_BUILD=1
      shift
      ;;
    --jobs)
      if [[ $# -lt 2 ]]; then
        echo "Error: --jobs requires a value" >&2
        exit 1
      fi
      JOBS="$2"
      shift 2
      ;;
    --skip-checks)
      SKIP_CHECKS=1
      shift
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    *)
      echo "Error: unknown option: $1" >&2
      usage
      exit 1
      ;;
  esac
done

if [[ -z "${JOBS}" ]]; then
  if command -v nproc >/dev/null 2>&1; then
    JOBS="$(nproc)"
  else
    JOBS=4
  fi
fi

check_command() {
  local name="$1"
  if ! command -v "${name}" >/dev/null 2>&1; then
    echo "  - missing command: ${name}" >&2
    return 1
  fi
  return 0
}

check_cxx_compiler() {
  if [[ -n "${CXX:-}" ]]; then
    check_command "${CXX}"
    return $?
  fi

  if command -v c++ >/dev/null 2>&1 || command -v g++ >/dev/null 2>&1 || command -v clang++ >/dev/null 2>&1; then
    return 0
  fi

  echo "  - missing C++ compiler: set CXX or provide c++/g++/clang++ in PATH" >&2
  return 1
}

if [[ "${SKIP_CHECKS}" -ne 1 ]]; then
  echo "[setup] Checking required build tools in current shell"
  missing=0
  check_command git || missing=1
  check_command cmake || missing=1
  check_cxx_compiler || missing=1

  if ! command -v make >/dev/null 2>&1 && ! command -v ninja >/dev/null 2>&1; then
    echo "  - missing build backend: provide make or ninja in PATH" >&2
    missing=1
  fi

  if [[ "${missing}" -ne 0 ]]; then
    cat >&2 <<'EOF'

Error: required build tools are missing from the current shell.
Install or load the project dependencies, then rerun this script.

Main dependencies:
  CMake 3.14+
  C++20 compiler
  Boost 1.71+
  Eigen3
  yaml-cpp
  OpenMP-capable compiler/runtime
  Git
  make or ninja
EOF
    exit 1
  fi
fi

echo "[setup] Initializing submodules"
git -C "${REPO_ROOT}" submodule update --init --recursive

if [[ "${BUILD_PROJECT}" -eq 1 ]]; then
  if [[ "${CLEAN_BUILD}" -eq 1 ]]; then
    echo "[setup] Removing build directory: ${REPO_ROOT}/${BUILD_DIR}"
    rm -rf "${REPO_ROOT:?}/${BUILD_DIR}"
  fi

  echo "[setup] Configuring project"
  cmake -S "${REPO_ROOT}" -B "${REPO_ROOT}/${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE}" \
    -DENABLE_OPENDB_BACKEND=OFF

  echo "[setup] Building project with ${JOBS} jobs"
  cmake --build "${REPO_ROOT}/${BUILD_DIR}" -j "${JOBS}"
fi

cat <<EOF

[setup] Done.

Repository:
  ${REPO_ROOT}

Build directory:
  ${REPO_ROOT}/${BUILD_DIR}

Typical ChipletPart command:
  cd ${REPO_ROOT}
  ./run_chiplet_test.sh ga100 --seed 1 --3dblox --legosim

EOF
