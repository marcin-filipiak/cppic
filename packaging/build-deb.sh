#!/usr/bin/env sh
#
# Build a binary .deb package for cppic.
#
#   packaging/build-deb.sh [--clean]
#
# Produces dist/ containing the compiled program, the runtime files and
# the cppic_<version>_<arch>.deb package.  Requires dpkg-deb and a C++
# toolchain; builds with CMAKE_BUILD_TYPE=Release.
set -e

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CMAKE="${CMAKE:-$(command -v cmake || true)}"
VER=$(sed -n 's/.*VERSION \([0-9]*\.[0-9]*\.[0-9]*\).*/\1/p' "$ROOT/CMakeLists.txt" | head -1)
DEBVER="${VER}-1"
ARCH=$(dpkg --print-architecture 2>/dev/null || echo amd64)

DIST="$ROOT/dist"
BIN="$ROOT/build-release/cppic"
STAGE="$DIST/cppic_${DEBVER}_${ARCH}"

if [ "$1" = "--clean" ]; then
    rm -rf "$ROOT/build-release" "$DIST"
fi

# 1) release build
if [ ! -x "$BIN" ]; then
    if [ -z "$CMAKE" ]; then
        echo "cppic: cmake not found (set CMAKE=/path/to/cmake)" >&2
        exit 1
    fi
    echo "== release build"
    "$CMAKE" -S "$ROOT" -B "$ROOT/build-release" -G Ninja -DCMAKE_BUILD_TYPE=Release >/dev/null
    "$CMAKE" --build "$ROOT/build-release" --target cppic >/dev/null
fi

# 2) stage the package tree
echo "== staging"
rm -rf "$STAGE"
mkdir -p "$STAGE/DEBIAN" \
         "$STAGE/usr/bin" \
         "$STAGE/usr/share/cppic/runtime" \
         "$STAGE/usr/share/doc/cppic" \
         "$STAGE/usr/share/man/man1"

strip --strip-unneeded -o "$STAGE/usr/bin/cppic" "$BIN"
cp "$ROOT/runtime/cppic_runtime.h" "$STAGE/usr/share/cppic/runtime/"
cp "$ROOT/runtime/cppic_runtime.c" "$STAGE/usr/share/cppic/runtime/"
cp "$ROOT/README.md"              "$STAGE/usr/share/doc/cppic/README.md"
cp "$ROOT/packaging/copyright"    "$STAGE/usr/share/doc/cppic/copyright"
cp "$ROOT/packaging/changelog"    "$STAGE/usr/share/doc/cppic/changelog.Debian"
gzip -n -9 -f "$STAGE/usr/share/doc/cppic/changelog.Debian"
cp "$ROOT/packaging/cppic.1"      "$STAGE/usr/share/man/man1/cppic.1"
gzip -n -9 -f "$STAGE/usr/share/man/man1/cppic.1"

# 3) control + md5sums
SIZE=$(du -sk "$STAGE/usr" | cut -f1)
sed -e "s/@VERSION@/$DEBVER/" \
    -e "s/@ARCH@/$ARCH/" \
    -e "s/@SIZE@/$SIZE/" \
    "$ROOT/packaging/control" > "$STAGE/DEBIAN/control"
( cd "$STAGE" && find usr -type f -exec md5sum {} + > DEBIAN/md5sums )

# 4) assemble the .deb
echo "== dpkg-deb"
dpkg-deb --root-owner-group --build "$STAGE" "$DIST/cppic_${DEBVER}_${ARCH}.deb" >/dev/null

# 5) standalone folder with the compiled program + runtime
echo "== dist/"
mkdir -p "$DIST/bin" "$DIST/runtime"
cp "$STAGE/usr/bin/cppic" "$DIST/bin/cppic"
cp "$STAGE/usr/share/cppic/runtime/cppic_runtime.h" "$DIST/runtime/"
cp "$STAGE/usr/share/cppic/runtime/cppic_runtime.c" "$DIST/runtime/"
cp "$ROOT/README.md" "$DIST/README.md"

echo "   dist/cppic_${DEBVER}_${ARCH}.deb"
echo "   dist/bin/cppic"
echo "   dist/runtime/"