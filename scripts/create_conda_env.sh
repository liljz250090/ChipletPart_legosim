#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: scripts/create_conda_env.sh [options]

Create a conda environment for ChipletPart_legosim and install the build
dependencies needed by README_LEGOSIM.md.

Options:
  --name NAME       Environment name. Default: chipletpart_legosim
  --force           Remove the environment first if it already exists.
  --build           Build ChipletPart_legosim after creating/updating the env.
  --jobs N          Build parallelism passed to setup_chipletpart_env.sh.
  --manager NAME    Package manager to use: conda or mamba. Default: conda.
  --help            Show this help.

Environment variables:
  CHIPLETPART_ENV_NAME   Alternative way to set the environment name.
  CONDA_SH               Path to conda.sh if auto-detection fails.
  BUILD_DIR              Build directory used when --build is set.
  CMAKE_BUILD_TYPE       CMake build type used when --build is set.

Examples:
  ./scripts/create_conda_env.sh
  ./scripts/create_conda_env.sh --name chipletpart --build --jobs 16
  ./scripts/create_conda_env.sh --force --build
EOF
}

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

ENV_NAME="${CHIPLETPART_ENV_NAME:-chipletpart_legosim}"
FORCE=0
BUILD=0
JOBS="${JOBS:-}"
MANAGER="conda"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --name)
      if [[ $# -lt 2 ]]; then
        echo "Error: --name requires a value" >&2
        exit 1
      fi
      ENV_NAME="$2"
      shift 2
      ;;
    --force)
      FORCE=1
      shift
      ;;
    --build)
      BUILD=1
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
    --manager)
      if [[ $# -lt 2 ]]; then
        echo "Error: --manager requires a value" >&2
        exit 1
      fi
      MANAGER="$2"
      shift 2
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

if [[ "${MANAGER}" != "conda" && "${MANAGER}" != "mamba" ]]; then
  echo "Error: --manager must be conda or mamba" >&2
  exit 1
fi

if [[ -z "${JOBS}" ]]; then
  if command -v nproc >/dev/null 2>&1; then
    JOBS="$(nproc)"
  else
    JOBS=4
  fi
fi

find_conda_sh() {
  if [[ -n "${CONDA_SH:-}" && -f "${CONDA_SH}" ]]; then
    echo "${CONDA_SH}"
    return 0
  fi

  local candidates=(
    "/opt/conda/miniforge3/etc/profile.d/conda.sh"
    "${HOME}/miniforge3/etc/profile.d/conda.sh"
    "${HOME}/mambaforge/etc/profile.d/conda.sh"
    "${HOME}/miniconda3/etc/profile.d/conda.sh"
    "${HOME}/anaconda3/etc/profile.d/conda.sh"
  )

  for candidate in "${candidates[@]}"; do
    if [[ -f "${candidate}" ]]; then
      echo "${candidate}"
      return 0
    fi
  done

  return 1
}

CONDA_SH_PATH="$(find_conda_sh || true)"
if [[ -z "${CONDA_SH_PATH}" ]]; then
  echo "Error: could not find conda.sh." >&2
  echo "Set CONDA_SH=/path/to/conda.sh and retry." >&2
  exit 1
fi

# shellcheck disable=SC1090
source "${CONDA_SH_PATH}"

if ! command -v "${MANAGER}" >/dev/null 2>&1; then
  echo "Error: package manager not found after loading conda: ${MANAGER}" >&2
  exit 1
fi

env_exists() {
  conda env list | awk '{print $1}' | grep -Fxq "$1"
}

packages=(
  cmake
  make
  ninja
  pkg-config
  git
  boost-cpp
  eigen
  yaml-cpp
  c-compiler
  cxx-compiler
  libgomp
)

if env_exists "${ENV_NAME}"; then
  if [[ "${FORCE}" -eq 1 ]]; then
    echo "[conda-env] Removing existing environment: ${ENV_NAME}"
    conda env remove -y -n "${ENV_NAME}"
  else
    echo "[conda-env] Environment already exists: ${ENV_NAME}"
  fi
fi

if ! env_exists "${ENV_NAME}"; then
  echo "[conda-env] Creating environment: ${ENV_NAME}"
  "${MANAGER}" create -y -n "${ENV_NAME}" -c conda-forge "${packages[@]}"
else
  echo "[conda-env] Updating dependencies in: ${ENV_NAME}"
  "${MANAGER}" install -y -n "${ENV_NAME}" -c conda-forge "${packages[@]}"
fi

conda activate "${ENV_NAME}"

echo "[conda-env] Tool versions"
cmake --version | head -1
if [[ -n "${CXX:-}" ]]; then
  "${CXX}" --version | head -1
else
  c++ --version | head -1
fi

if [[ "${BUILD}" -eq 1 ]]; then
  echo "[conda-env] Building ChipletPart_legosim"
  "${REPO_ROOT}/scripts/setup_chipletpart_env.sh" --build --jobs "${JOBS}"
fi

cat <<EOF

[conda-env] Done.

To use this environment:
  source ${CONDA_SH_PATH}
  conda activate ${ENV_NAME}

Build command:
  cd ${REPO_ROOT}
  ./scripts/setup_chipletpart_env.sh --build --jobs ${JOBS}

Run command:
  ./run_chiplet_test.sh ga100 --seed 1 --3dblox --legosim --threads 16

EOF
