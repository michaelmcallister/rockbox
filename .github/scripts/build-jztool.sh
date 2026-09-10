#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "$0")/../.." && pwd)
work=${1:?Usage: build-jztool.sh BUILD_DIR OUTPUT_DIR}
out=${2:?Usage: build-jztool.sh BUILD_DIR OUTPUT_DIR}
jobs=${JOBS:-$(getconf _NPROCESSORS_ONLN)}
libusb_version=1.0.30
libusb_sha256=fea36f34f9156400209595e300840767ab1a385ede1dc7ee893015aea9c6dbaf

if [[ -f "$root/REVISION.txt" ]]; then
    revision=$(cat "$root/REVISION.txt")
else
    revision=$(git -C "$root" rev-parse HEAD)
fi

mkdir -p "$work" "$out"
work=$(cd "$work" && pwd)
out=$(cd "$out" && pwd)
archive="libusb-$libusb_version.tar.bz2"
curl --fail --location --retry 3 --output "$work/$archive" \
    "https://github.com/libusb/libusb/releases/download/v$libusb_version/$archive"
echo "$libusb_sha256  $work/$archive" | shasum -a 256 -c -
tar -xjf "$work/$archive" -C "$work"

case $(uname -s) in
    Darwin)
        export MACOSX_DEPLOYMENT_TARGET=11.0
        flags=("GCCFLAGS=-DVERSION=\\\"${revision:0:10}\\\" -mmacosx-version-min=11.0"
               "LDFLAGS=-mmacosx-version-min=11.0")
        ;;
    Linux) flags=("LDFLAGS=-static") ;;
    *) echo "Unsupported host" >&2; exit 1 ;;
esac

(
    cd "$work/libusb-$libusb_version"
    ./configure --prefix="$work/deps" --disable-shared --enable-static --disable-udev
    make -j"$jobs"
    make install
)
export PKG_CONFIG_LIBDIR="$work/deps/lib/pkgconfig"
export PKG_CONFIG_PATH="$PKG_CONFIG_LIBDIR"
make -C "$root/utils/jztool" -j"$jobs" RELEASE=1 STATIC=1 APPVERSION="${revision:0:10}" \
    BUILD_DIR="$work/objects" "${flags[@]}" \
    LIBUSB_CFLAGS="$(pkg-config --cflags libusb-1.0)" \
    LIBUSB_LDOPTS="$(pkg-config --static --libs libusb-1.0)"
cp "$root/utils/jztool/jztool" "$out/jztool"

if [[ $(uname -s) == Darwin ]]; then
    codesign --force --sign - "$out/jztool"
    otool -L "$out/jztool" | awk 'NR > 1 {print $1}' > "$work/libraries.txt"
    test -s "$work/libraries.txt"
    if grep -Ev '^(/usr/lib/|/System/Library/)' "$work/libraries.txt"; then
        echo "jztool depends on a non-system library" >&2
        exit 1
    fi
else
    if readelf -l "$out/jztool" | grep -q INTERP; then
        echo "jztool is not statically linked" >&2
        exit 1
    fi
fi

# Help exits with status 4 and must not attempt to open a USB device.
status=0
"$out/jztool" hibyr1 > "$work/help.txt" || status=$?
test "$status" -eq 4
grep -q 'jztool hibyr1 load <bootloader.r1>' "$work/help.txt"

cp "$root/docs/COPYING" "$out/COPYING-jztool.txt"
cp "$work/libusb-$libusb_version/COPYING" "$out/COPYING-libusb.txt"
cp "$work/$archive" "$out/"
