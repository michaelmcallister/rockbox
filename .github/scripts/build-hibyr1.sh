#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "$0")/../.." && pwd)
work=${1:?Usage: build-hibyr1.sh BUILD_DIR OUTPUT_DIR}
out=${2:?Usage: build-hibyr1.sh BUILD_DIR OUTPUT_DIR}
jobs=${JOBS:-$(getconf _NPROCESSORS_ONLN)}

if [[ -f "$root/REVISION.txt" ]]; then
    revision=$(cat "$root/REVISION.txt")
    export VERSION=${revision:0:10}
else
    revision=$(git -C "$root" rev-parse HEAD)
fi

mkdir -p "$work" "$out/firmware" "$out/symbols"
work=$(cd "$work" && pwd)
out=$(cd "$out" && pwd)

for kind in firmware bootloader; do
    test ! -e "$work/$kind/Makefile" || {
        echo "Use a fresh build directory: $work/$kind" >&2
        exit 1
    }
    mkdir -p "$work/$kind"
done

(
    cd "$work/firmware"
    "$root/tools/configure" --target=hibyr1native --type=n
    make -j"$jobs" fullzip
)
(
    cd "$work/bootloader"
    "$root/tools/configure" --target=hibyr1native --type=b
    make -j"$jobs"
)

cp "$work/firmware/rockbox-full.zip" "$out/firmware/rockbox-hibyr1.zip"
cp "$work/firmware/rockbox.r1" "$work/bootloader/bootloader.r1" "$out/firmware/"
cp "$root/docs/hibyr1-builds.md" "$out/firmware/README.md"
echo "$revision" > "$out/firmware/REVISION.txt"
if [[ ! -f "$root/REVISION.txt" ]]; then
    git -C "$root" archive --format=tar.gz --add-file="$out/firmware/REVISION.txt" \
        -o "$out/firmware/rockbox-source.tar.gz" HEAD
fi

for name in rockbox bootloader spl usbstage1; do
    dir=bootloader
    test "$name" != rockbox || dir=firmware
    gzip -c "$work/$dir/$name.elf" > "$out/symbols/$name.elf.gz"
    cp "$work/$dir/$name.map" "$out/symbols/"
done
cp "$out/firmware/REVISION.txt" "$out/symbols/"
python3 "$root/.github/scripts/verify-hibyr1.py" "$out/firmware"
