#!/usr/bin/env bash
# Cross-compiles the co-op plugin (Windows x64 DLL) with MinGW-w64.
set -euo pipefail
cd -- "$(dirname -- "$0")"
CXX="${CXX:-x86_64-w64-mingw32-g++}"
mkdir -p package
# Release builds pass COOP_VERSION; local builds are marked as development builds.
coop_version="${COOP_VERSION:-$(tr -d '[:space:]' < version.txt)-dev}"
case "$coop_version" in *[!0-9A-Za-z.+-]*) echo "invalid COOP_VERSION: $coop_version" >&2; exit 1 ;; esac
"$CXX" -std=c++20 -O2 -Wall -Wextra -Werror -shared -static -s \
  -DCOOP_VERSION="\"${coop_version}\"" \
  -o package/TheysGone.asi src/coop/*.cpp \
  -lws2_32 -lbcrypt -Wl,--subsystem,windows -Wl,--no-insert-timestamp
mkdir -p package/BendGame/Content/Paks
cp assets/branding/pakchunk99-TheysGone_Brand_P.pak package/BendGame/Content/Paks/
echo "built package/TheysGone.asi ($coop_version)"
