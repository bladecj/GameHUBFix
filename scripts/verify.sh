#!/usr/bin/env bash
# Build from src/ and run all standalone tests. No Rainmeter instance needed.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
CXX="${CXX:-x86_64-w64-mingw32-g++}"
OUT="GameHUBFix.verify.dll"

# ctypes.CDLL needs an absolute Windows path on some setups.
if pwd -W >/dev/null 2>&1; then
  ROOT="$(pwd -W)"
else
  ROOT="$(pwd)"
fi
DLL="$ROOT/$OUT"

cleanup() { rm -f "$OUT" SessionGameTest.exe ghf_probe.exe; }
trap cleanup EXIT

echo "=== build plugin + test helpers ==="
"$CXX" -O2 -std=c++11 -shared -static-libgcc -static-libstdc++ \
       -o "$OUT" src/GameHUBFix.cpp -lpsapi
"$CXX" -O2 -municode -o SessionGameTest.exe tests/fake_game.cpp
"$CXX" -O2 -municode -o ghf_probe.exe tests/ghf_probe.cpp

echo
echo "=== A: generic gate (no game names) ==="
./ghf_probe.exe "$DLL" "$ROOT/SessionGameTest.exe"

echo
echo "=== B: taskbar step-aside / restore ==="
python tests/ghf_layer_e2e.py "$DLL"

echo
echo "=== C: launch-exit loop + refresh stress + Update() latency ==="
python tests/ghf_v2_stress.py "$DLL"

echo
echo "ALL TESTS DONE"
