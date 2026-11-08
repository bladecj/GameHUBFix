#!/usr/bin/env bash
# Build GameHUBFix.dll with MinGW-w64 (x86_64). Run from anywhere.
set -euo pipefail

# Resolve the project root from this script's own location, so the build works
# from any working directory. Use an absolute output path: MinGW GCC creates
# temporary files next to the -o target and can misjoin relative MSYS paths.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
CXX="${CXX:-x86_64-w64-mingw32-g++}"
OBJDUMP="${OBJDUMP:-objdump}"
OUT="${OUT:-$ROOT/GameHUBFix.dll}"

echo "compiler : $CXX"
echo "output   : $OUT"

"$CXX" -O2 -std=c++11 -shared \
       -static-libgcc -static-libstdc++ \
       -o "$OUT" \
       src/GameHUBFix.cpp \
       -lpsapi

echo
echo "--- exported symbols (expect 6) ---"
"$OBJDUMP" -p "$OUT" | sed -n '/Ordinal\/Name Pointer/,/^$/p'
echo
echo "Built successfully: $OUT"
echo "Deploy with scripts/deploy.bat (quit Rainmeter first)."
