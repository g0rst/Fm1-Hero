#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Baut die Tonfabrik mit dem Build-Werkzeug von fm1-base (base/, ein Git-Submodul: git submodule update --init --recursive).
#   ./build.sh                 das Firmware-Paket app/build/tonfabrik.fwsc
#                              (braucht JieLis Toolchain und das AC79-SDK: base/README.md, base/tools/get_toolchain.sh)
#   ./build.sh --host          den Host-Simulator von fm1-base: app/build/host/tonfabrik_host
#   ./build.sh --release X.Y   ein Release-Paket
# Die Browser-Version baut sim/build.sh, die Tests des Kerns tests/run_tests.sh.
set -e
cd "$(dirname "$0")"
[ -f base/tools/build.py ] || { echo "base/ ist leer: git submodule update --init --recursive" >&2; exit 1; }
exec python3 base/tools/build.py app "$@"
