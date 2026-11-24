#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Baut die Browser-Version: der Kern als WebAssembly (clang mit dem Ziel wasm32 und wasm-ld; ohne Emscripten, ohne
# libc), eingebettet in sim/page.html -> docs/index.html, eine Datei, die alles enthält.
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/build/sim"
mkdir -p "$OUT" "$ROOT/docs"
${CLANG:-clang} --target=wasm32 -O2 -ffreestanding -nostdlib -fno-builtin -Wall -Wno-unused-function \
    -Wl,--no-entry -Wl,--strip-all -Wl,-z,stack-size=65536 \
    -o "$OUT/fab.wasm" "$ROOT/sim/wasm.c" "$ROOT/app/core/fab.c"
python3 - "$ROOT/sim/page.html" "$OUT/fab.wasm" "$ROOT/docs/index.html" <<'PY'
import base64, sys
page, wasm, dest = sys.argv[1:]
html = open(page, encoding="utf-8").read()
assert html.count("@@WASM@@") == 1
open(dest, "w", encoding="utf-8").write(html.replace("@@WASM@@", base64.b64encode(open(wasm, "rb").read()).decode()))
import os, shutil
shutil.copyfile(dest, os.path.join(os.path.dirname(dest), "testseite.html"))   # dieselbe Seite, wie bei ro:MINImal
print(f"{dest}: {len(html)} Bytes Seite, {len(open(wasm, 'rb').read())} Bytes WebAssembly")
PY
