#!/bin/sh
# Install the GitHub-built libhybris bundle INSIDE the ARM64 Ubuntu rootfs and
# select it as the guest GPU driver. Nothing under Android /system or /vendor is
# modified; the Android vendor driver is only loaded from its original location.
set -eu
bundle=$(cd "${1:?usage: sh install-hybris.sh /path/to/extracted-libhybris-glibc-arm64-artifact}" && pwd)
test "$(uname -m)" = aarch64
test "$(cat "$bundle/SOURCE_COMMIT")" = 7079712a42ea2754adf747e70c6cc75764c8596e
(cd "$bundle" && sha256sum -c SHA256SUMS)
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT HUP INT TERM
tar -xzf "$bundle/libhybris-glibc-arm64.tar.gz" -C "$stage"
test -f "$stage/usr/lib/hybris/lib/libvulkan.so.1"
test -f "$stage/usr/lib/hybris/lib/libhybris/linker/q.so"
rm -rf /usr/lib/hybris.new
mkdir -p /usr/lib /usr/local/bin /etc/profile.d
mv "$stage/usr/lib/hybris" /usr/lib/hybris.new
rm -rf /usr/lib/hybris
mv /usr/lib/hybris.new /usr/lib/hybris
install -m 755 /usr/lib/hybris/share/proroot-driver /usr/lib/hybris/share/proroot-hybris /usr/local/bin/
install -m 644 /usr/lib/hybris/share/profile.sh /etc/profile.d/proroot-hybris.sh
printf '%s\n' hybris > /etc/proroot-driver
printf '%s\n' 'Installed and selected libhybris. Stop and restart the terminal session,'
printf '%s\n' 'then run: /usr/lib/hybris/bin/proroot-hybris-probe'
printf '%s\n' 'Return to RADV with: proroot-driver radv'
