#!/usr/bin/env bash
# Build and run the EUC+ host tests with strict warnings (no toolchain or firmware needed).
#   bash tools/host_tests.sh [PREVIEW_DIR]     (PREVIEW_DIR: also write the UI screens as .pbm)
set -euo pipefail
MOD="$(cd "$(dirname "$0")/.." && pwd)/mods/eucplus-dt2"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT
CFLAGS="-std=c99 -O1 -Wall -Wextra -Werror -DEUP_HOST"
cc $CFLAGS -o "$OUT/host_test" "$MOD/test/host_test.c" "$MOD/eucplus.c"
cc $CFLAGS -o "$OUT/ui_host_test" "$MOD/test/ui_host_test.c" "$MOD/ui.c" "$MOD/eucplus.c"
"$OUT/host_test" > "$OUT/notes.txt" && echo "host_test: OK ($(grep -c pass "$OUT/notes.txt") passes printed)"
"$OUT/ui_host_test" ${1:+"$1"}
