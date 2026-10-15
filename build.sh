#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 Abraham Joseph Okayli Masaryk
# =============================================================================
#  MatterTone POC — build.sh
#  Robust local build on Apple Silicon. Pins Apple clang and the Xcode SDK, and
#  scrubs environment variables that a conda/miniforge env (or other toolchains)
#  can inject and which would otherwise pollute the JUCE build with foreign
#  headers / rpaths. Pass extra CMake args through, e.g.:
#     ./build.sh -DMATTER_USE_FOLEYS=ON -DMATTER_USE_FCWT=ON
# =============================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="${ROOT}/build"
SDK="$(xcrun --show-sdk-path)"

# --universal builds arm64 + x86_64 (final/release); default arm64 (dev loop).
ARCHS="arm64"
if [[ "${1:-}" == "--universal" ]]; then ARCHS="arm64;x86_64"; shift; fi

# Prefer Ninja if present, else Unix Makefiles.
if command -v ninja >/dev/null 2>&1; then GEN=(-G Ninja); else GEN=(-G "Unix Makefiles"); fi

# Scrub toolchain-contaminating environment variables (conda/miniforge etc.).
unset CFLAGS CXXFLAGS CPPFLAGS LDFLAGS LDFLAGS_LD DEBUG_CFLAGS \
      C_INCLUDE_PATH CPLUS_INCLUDE_PATH CPATH OBJC_INCLUDE_PATH \
      LIBRARY_PATH DYLD_LIBRARY_PATH PKG_CONFIG_PATH 2>/dev/null || true

cmake -S "${ROOT}" -B "${BUILD}" "${GEN[@]}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=/usr/bin/clang \
    -DCMAKE_CXX_COMPILER=/usr/bin/clang++ \
    -DCMAKE_OSX_ARCHITECTURES="${ARCHS}" \
    -DCMAKE_OSX_SYSROOT="${SDK}" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
    "$@"

cmake --build "${BUILD}" --config Release -j4

echo
echo "=== build complete ==="
echo "VST3:       ${BUILD}/MatterTone_artefacts/Release/VST3/MatterTone.vst3"
echo "Standalone: ${BUILD}/MatterTone_artefacts/Release/Standalone/MatterTone.app"
echo "AU:         ${BUILD}/MatterTone_artefacts/Release/AU/MatterTone.component"
echo "Harness:    ${BUILD}/matter_render"
