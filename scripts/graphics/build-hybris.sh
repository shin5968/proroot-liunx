#!/bin/bash
# Build the pinned libhybris for the glibc guest. Run on a native builder whose
# glibc is not newer than the guest's (Ubuntu 24.04 for the default rootfs).
# usage: build-hybris.sh WORK_DIR OUTPUT_DIR
set -euo pipefail
work=${1:?usage: build-hybris.sh WORK_DIR OUTPUT_DIR}
out=${2:?usage: build-hybris.sh WORK_DIR OUTPUT_DIR}
here=$(cd "$(dirname "$0")" && pwd)
HYBRIS_COMMIT=7079712a42ea2754adf747e70c6cc75764c8596e
HEADERS_COMMIT=217a38e92cc64a96b4d1ac7469f30827d84485d3
case "$(uname -m)" in
    aarch64) arch=arm64 ;;
    x86_64) arch=x86-64 ;; # build check only; the guest bundle must be ARM64
    *) echo "unsupported builder architecture" >&2; exit 1 ;;
esac
fetch() { # url commit directory
    git init -q "$3"
    git -C "$3" remote add origin "$1"
    git -C "$3" fetch -q --depth=1 origin "$2"
    git -C "$3" checkout -q --detach FETCH_HEAD
    test "$(git -C "$3" rev-parse HEAD)" = "$2"
}
mkdir -p "$work" "$out"
work=$(cd "$work" && pwd); out=$(cd "$out" && pwd)
fetch https://github.com/libhybris/libhybris.git "$HYBRIS_COMMIT" "$work/libhybris"
fetch https://github.com/Halium/android-headers.git "$HEADERS_COMMIT" "$work/android-headers"
cd "$work/libhybris/hybris"
NOCONFIGURE=1 ./autogen.sh
./configure --prefix=/usr/lib/hybris --enable-arch="$arch" --enable-wayland \
    --with-android-headers="$work/android-headers" --with-default-egl-platform=wayland
make -j"$(nproc)"
make install DESTDIR="$work/stage"
stage="$work/stage/usr/lib/hybris"
# Headers, static archives and libtool files are not needed in the guest.
rm -rf "$stage/include" "$stage/lib/pkgconfig" "$stage/share"
find "$stage" \( -name '*.la' -o -name '*.a' \) -delete
# Offscreen probe bound to the libhybris loader, independent of LD_LIBRARY_PATH.
cc -O2 -Wall -Wextra -Werror "$here/hybris-probe.c" -o "$stage/bin/proroot-hybris-probe" \
    -L"$stage/lib" -lvulkan -Wl,--enable-new-dtags,-rpath,/usr/lib/hybris/lib,-rpath-link,"$stage/lib"
mkdir -p "$stage/share"
install -m 755 "$here/hybris/proroot-driver" "$here/hybris/proroot-hybris" "$stage/share/"
install -m 644 "$here/hybris/profile.sh" "$stage/share/"
mkdir -p "$stage/licenses"
cp "$work"/libhybris/LICENSE.* "$stage/licenses/"
printf '%s\n' "$HYBRIS_COMMIT" > "$stage/SOURCE_COMMIT"
printf '%s\n' "$HEADERS_COMMIT" > "$stage/ANDROID_HEADERS_COMMIT"
tar -C "$work/stage" -czf "$out/libhybris-glibc-$arch.tar.gz" usr/lib/hybris
printf '%s\n' "$HYBRIS_COMMIT" > "$out/SOURCE_COMMIT"
cp "$here/install-hybris.sh" "$out/"
(cd "$out" && sha256sum "libhybris-glibc-$arch.tar.gz" > SHA256SUMS)
