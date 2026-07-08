#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: scripts/install_chipletpart_deps.sh [options]

Generate or run system package installation commands for ChipletPart_legosim.
This script only uses the detected system package manager.

By default, the script only prints the detected install command. Use --execute
to run it.

Options:
  --execute         Run the detected install command.
  --no-sudo         Do not prefix the command with sudo.
  --yes             Pass non-interactive yes flags to the package manager.
  --build-after     Run scripts/setup_chipletpart_env.sh --build after install.
  --help            Show this help.

Supported package managers:
  apt-get
  dnf
  yum
  pacman
  brew

Examples:
  ./scripts/install_chipletpart_deps.sh
  ./scripts/install_chipletpart_deps.sh --execute --yes
  ./scripts/install_chipletpart_deps.sh --execute --yes --build-after
  ./scripts/install_chipletpart_deps.sh --execute --no-sudo
EOF
}

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

EXECUTE=0
USE_SUDO=1
ASSUME_YES=0
BUILD_AFTER=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --execute)
      EXECUTE=1
      shift
      ;;
    --no-sudo)
      USE_SUDO=0
      shift
      ;;
    --yes|-y)
      ASSUME_YES=1
      shift
      ;;
    --build-after)
      BUILD_AFTER=1
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

require_command() {
  local name="$1"
  if ! command -v "${name}" >/dev/null 2>&1; then
    echo "Error: required command not found: ${name}" >&2
    exit 1
  fi
}

sudo_prefix=()
if [[ "${USE_SUDO}" -eq 1 && "$(id -u)" -ne 0 ]]; then
  sudo_prefix=(sudo)
fi

pkg_manager=""
install_cmd=()
update_cmd=()

if command -v apt-get >/dev/null 2>&1; then
  pkg_manager="apt-get"
  yes_flag=()
  [[ "${ASSUME_YES}" -eq 1 ]] && yes_flag=(-y)
  update_cmd=("${sudo_prefix[@]}" apt-get update)
  install_cmd=("${sudo_prefix[@]}" apt-get install "${yes_flag[@]}" \
    build-essential cmake git pkg-config make ninja-build \
    libboost-all-dev libeigen3-dev libyaml-cpp-dev libomp-dev)
elif command -v dnf >/dev/null 2>&1; then
  pkg_manager="dnf"
  yes_flag=()
  [[ "${ASSUME_YES}" -eq 1 ]] && yes_flag=(-y)
  install_cmd=("${sudo_prefix[@]}" dnf install "${yes_flag[@]}" \
    gcc gcc-c++ cmake git pkgconf-pkg-config make ninja-build \
    boost-devel eigen3-devel yaml-cpp-devel libgomp)
elif command -v yum >/dev/null 2>&1; then
  pkg_manager="yum"
  yes_flag=()
  [[ "${ASSUME_YES}" -eq 1 ]] && yes_flag=(-y)
  install_cmd=("${sudo_prefix[@]}" yum install "${yes_flag[@]}" \
    gcc gcc-c++ cmake git pkgconfig make ninja-build \
    boost-devel eigen3-devel yaml-cpp-devel libgomp)
elif command -v pacman >/dev/null 2>&1; then
  pkg_manager="pacman"
  yes_flag=()
  [[ "${ASSUME_YES}" -eq 1 ]] && yes_flag=(--noconfirm)
  install_cmd=("${sudo_prefix[@]}" pacman -S "${yes_flag[@]}" \
    base-devel cmake git pkgconf make ninja \
    boost eigen yaml-cpp openmp)
elif command -v brew >/dev/null 2>&1; then
  pkg_manager="brew"
  install_cmd=(brew install \
    cmake git pkg-config make ninja \
    boost eigen yaml-cpp libomp)
fi

if [[ -z "${pkg_manager}" ]]; then
  cat >&2 <<'EOF'
Error: no supported package manager was found.

Install these dependencies manually:
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

print_command() {
  printf '  '
  printf '%q ' "$@"
  printf '\n'
}

echo "[install] Detected package manager: ${pkg_manager}"

if [[ "${#update_cmd[@]}" -gt 0 ]]; then
  echo "[install] Update command:"
  print_command "${update_cmd[@]}"
fi

echo "[install] Install command:"
print_command "${install_cmd[@]}"

if [[ "${EXECUTE}" -ne 1 ]]; then
  cat <<'EOF'

[install] Dry run only. Add --execute to run the command.
EOF
  exit 0
fi

if [[ "${USE_SUDO}" -eq 1 && "$(id -u)" -ne 0 ]]; then
  require_command sudo
fi

if [[ "${#update_cmd[@]}" -gt 0 ]]; then
  echo "[install] Updating package metadata"
  "${update_cmd[@]}"
fi

echo "[install] Installing dependencies"
"${install_cmd[@]}"

if [[ "${BUILD_AFTER}" -eq 1 ]]; then
  echo "[install] Building ChipletPart_legosim"
  "${REPO_ROOT}/scripts/setup_chipletpart_env.sh" --build
fi

echo "[install] Done."
