#!/usr/bin/env bash
set -euo pipefail
repo=$(cd "$(dirname "$0")/../.." && pwd)
work=${RUNNER_TEMP:-/tmp}/proroot-wayland-build
out=${1:?usage: build.sh OUTPUT_DIRECTORY}
mkdir -p "$work" "$out"
git init "$work/source"
git -C "$work/source" remote add origin https://github.com/wayland-mirror/wayland.git
git -C "$work/source" fetch --depth=1 origin 736d12ac67c20c60dc406dc49bb06be878501f86
git -C "$work/source" checkout --detach FETCH_HEAD
meson setup "$work/build" "$work/source" --prefix="$work/install" --libdir=lib \
  -Ddefault_library=static -Ddocumentation=false -Dtests=false -Ddtd_validation=false
ninja -C "$work/build" install
scanner="$work/install/bin/wayland-scanner"
xml="$repo/scripts/wayland/xdg-shell.xml"
"$scanner" server-header "$xml" "$work/xdg-shell-server-protocol.h"
"$scanner" client-header "$xml" "$work/xdg-shell-client-protocol.h"
"$scanner" private-code "$xml" "$work/xdg-shell-protocol.c"
flags=(-std=c11 -O2 -Wall -Wextra -Werror -I"$work" -I"$work/install/include" -I"$repo/app/src/main/cpp")
ffi=$(cc -print-file-name=libffi.a)
test -f "$ffi"
cc "${flags[@]}" "$repo/scripts/wayland/compositor.c" "$work/xdg-shell-protocol.c" \
  "$work/install/lib/libwayland-server.a" "$ffi" -lm -pthread -o "$out/proroot-wayland"
cc "${flags[@]}" "$repo/scripts/wayland/demo.c" "$work/xdg-shell-protocol.c" \
  "$work/install/lib/libwayland-client.a" "$ffi" -lm -pthread -o "$out/proroot-wayland-demo"
cp "$work/source/COPYING" "$out/WAYLAND-LICENSE"
cp /usr/share/doc/libffi8/copyright "$out/LIBFFI-LICENSE"
cp "$xml" "$out/xdg-shell.xml"
printf '%s\n' 'Wayland 1.24.0: 736d12ac67c20c60dc406dc49bb06be878501f86' > "$out/SOURCES"
printf '%s\n' 'xdg-shell: wayland-protocols 1.43 (vendored XML with upstream license)' >> "$out/SOURCES"
readelf -d "$out/proroot-wayland" > "$out/compositor-dynamic.txt"
readelf -d "$out/proroot-wayland-demo" > "$out/demo-dynamic.txt"
(cd "$out" && sha256sum proroot-wayland proroot-wayland-demo > SHA256SUMS)
