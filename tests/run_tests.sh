#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Die Host-Tests der Tonfabrik: baut tests/test_core.c und tests/test_story.c mit dem System-Compiler und führt sie aus.
# Schreibt Bildschirmfotos (PPM) und live.wav nach build/tests/ und build/story/.
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/build/tests"
mkdir -p "$OUT" "$ROOT/build/story"
CFLAGS="-std=c99 -O2 -Wall -Wextra -Wno-unused-function -Wno-type-limits"
${CC:-cc} $CFLAGS -I"$ROOT/app/core" "$ROOT/tests/test_core.c" "$ROOT/app/core/fab.c" -lm -o "$OUT/test_core"
${CC:-cc} $CFLAGS -I"$ROOT/app/core" "$ROOT/tests/test_story.c" -lm -o "$OUT/test_story"
${CC:-cc} $CFLAGS -I"$ROOT/app/core" "$ROOT/tests/test_cpu.c" -lm -o "$OUT/test_cpu"
"$OUT/test_core" "$OUT"
"$OUT/test_story" "$ROOT/build/story"
"$OUT/test_cpu"
# die App im Host-Simulator von fm1-base, wenn er gebaut ist (./build.sh --host)
SIM="$ROOT/app/build/host/tonfabrik_host"
if [ -x "$SIM" ]; then
    "$SIM" "$ROOT/app/tests/basic.txt" "$ROOT/app/build/host" | tee "$OUT/sim.log" | tail -2
    ! grep -q "EXPECTATIONS FAILED" "$OUT/sim.log"
else
    echo "Host-Simulator nicht gebaut (./build.sh --host): übersprungen"
fi
