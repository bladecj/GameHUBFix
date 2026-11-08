#!/usr/bin/env bash
# Build GameHUBFix.dll with MinGW-w64 (x86_64). Run from anywhere.
set -euo pipefail

cd "$(dirname "$0")/.."
CXX="${CXX:-x86_64-w64-mingw32-g++}"
OBJDUMP="${OBJDUMP:-objdump}"
OUT="${OUT:-GameHUBFix.dll}"

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
echo "Built successfully: $(pwd)/$OUT"
echo "Deploy with scripts/deploy.bat (quit Rainmeter first)."
