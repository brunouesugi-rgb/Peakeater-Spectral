#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build/macos-au"

cd "${ROOT_DIR}"

if ! command -v conan >/dev/null 2>&1; then
    echo "conan is required. Install Conan 2 first: python3 -m pip install --user conan"
    exit 1
fi

GENERATOR="Unix Makefiles"
if command -v ninja >/dev/null 2>&1; then
    GENERATOR="Ninja"
fi

conan install . --output-folder="${BUILD_DIR}" --build=missing -s build_type=Release

cmake -S . -B "${BUILD_DIR}" -G "${GENERATOR}" \
    -DCMAKE_TOOLCHAIN_FILE="${BUILD_DIR}/generators/conan_toolchain.cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PROJECT_NAME=peakeater_spectral \
    -DCMAKE_PROJECT_VERSION=0.1.0 \
    -DCONAN_PROJECT_COMPANY="AXLRTR Audio Lab" \
    -DCONAN_PROJECT_PRODUCT_NAME="Peakeater Spectral Beta" \
    -DCONAN_PROJECT_URL="https://github.com/axlrtr-audio-lab/peakeater-spectral"

cmake --build "${BUILD_DIR}" --config Release --target peakeater_spectral_2_AU

COMPONENT_PATH="$(find "${BUILD_DIR}" -name 'Peakeater Spectral 2.component' -type d | head -n 1)"
if [[ -z "${COMPONENT_PATH}" ]]; then
    echo "AU build finished but Peakeater Spectral 2.component was not found."
    exit 1
fi

echo "Built AU:"
echo "${COMPONENT_PATH}"
