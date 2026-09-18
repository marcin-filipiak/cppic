#!/usr/bin/env sh
# cppic build helpers
#
#   scripts/build.sh sketch.cpp [more.cpp ...]   build & run on the desktop simulator
#   scripts/build.sh --hostsim sketch.cpp [..]   (same, explicit)
#   scripts/build.sh --sdcc sketch.cpp [..]      build a PIC18 Intel HEX with sdcc
#
# Multiple source files are preprocessed and linked into one translation unit;
# relative #include "..." headers are resolved and inlined automatically.
# The host simulator compiles the generated C with the runtime in
# CPPIC_HOST_SIM mode; the program runs and exits non-zero if the sketch
# never wrote a non-zero PORTB.  The SDCC backend requires `sdcc` on PATH
# (apt install sdcc) and emits sketch.hex for the configured device.
set -e

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CPPIC="$ROOT/build/cppic"
RUNTIME="$ROOT/runtime"
SDCC_DEVICE="${CPPIC_SDCC_DEVICE:-18f87k22}"

if [ ! -x "$CPPIC" ]; then
    echo "cppic: build the transpiler first:  cmake -B build && cmake --build build" >&2
    exit 1
fi

usage() {
    sed -n '2,11p' "$0" | sed 's/^# \{0,1\}//'
    exit 1
}

MODE=hostsim
while [ $# -gt 0 ]; do
    case "$1" in
        --hostsim) MODE=hostsim; shift ;;
        --sdcc)    MODE=sdcc; shift ;;
        -h|--help) usage ;;
        -*) echo "cppic: unknown option $1" >&2; usage ;;
        *) break ;;
    esac
done
[ $# -eq 0 ] && usage

case "$MODE" in
    hostsim)
        base=$(basename "$1" .cpp)
        out="${base}_host"
        "$CPPIC" --emit "$@" > "$base.c"
        cc -DCPPIC_HOST_SIM -I "$RUNTIME" -o "$out" \
            "$base.c" "$RUNTIME/cppic_runtime.c"
        echo "== [$*] -> $base.c -> $out"
        "./$out" && echo "   $base: PORTB toggled OK"
        ;;
    sdcc)
        base=$(basename "$1" .cpp)
        "$CPPIC" --emit "$@" > "$base.c"
        sdcc -mpic18 -p"$SDCC_DEVICE" -I "$RUNTIME" \
            "$base.c" "$RUNTIME/cppic_runtime.c"
        echo "== [$*] -> $base.hex (${SDCC_DEVICE})"
        ;;
esac