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
cat > /usr/local/bin/proroot-driver <<'SELECT'
#!/bin/sh
# Select the guest GPU driver for NEW app sessions: hybris | radv | none
file=/etc/proroot-driver
case "${1:-status}" in
    hybris) test -f /usr/lib/hybris/lib/libvulkan.so.1 || { echo "libhybris is not installed" >&2; exit 1; } ;;
    radv) test -f /usr/lib/xclipse/libvulkan_radeon.so || { echo "RADV is not installed" >&2; exit 1; } ;;
    none) ;;
    status) echo "selected: $(cat "$file" 2>/dev/null || echo auto)"; exit 0 ;;
    *) echo "usage: proroot-driver hybris|radv|none|status" >&2; exit 2 ;;
esac
printf '%s\n' "$1" > "$file"
echo "Selected $1. Stop and restart the terminal session to apply."
SELECT
chmod 755 /usr/local/bin/proroot-driver
cat > /usr/local/bin/proroot-hybris <<'WRAPPER'
#!/bin/sh
# Run one command with the Android vendor driver through libhybris.
unset VK_DRIVER_FILES VK_ICD_FILENAMES PROROOT_NATIVE_DRM PROROOT_DRM_RENDER_NODE
export LD_LIBRARY_PATH=/usr/lib/hybris/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
export HYBRIS_EGLPLATFORM=${HYBRIS_EGLPLATFORM:-wayland}
export HYBRIS_VULKANPLATFORM=${HYBRIS_VULKANPLATFORM:-wayland}
if [ "$#" = 0 ]; then set -- /usr/lib/hybris/bin/test_vulkan; fi
exec "$@"
WRAPPER
chmod 755 /usr/local/bin/proroot-hybris
cat > /etc/profile.d/proroot-hybris.sh <<'PROFILE'
# Sorts after proroot-radv.sh, so an explicit hybris selection wins in login shells.
if [ "$(cat /etc/proroot-driver 2>/dev/null)" = hybris ] && [ -r /usr/lib/hybris/lib/libvulkan.so.1 ]; then
    unset VK_DRIVER_FILES VK_ICD_FILENAMES PROROOT_NATIVE_DRM PROROOT_DRM_RENDER_NODE
    case ":${LD_LIBRARY_PATH:-}:" in
        *:/usr/lib/hybris/lib:*) ;;
        *) export LD_LIBRARY_PATH=/usr/lib/hybris/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH} ;;
    esac
    export HYBRIS_EGLPLATFORM=wayland HYBRIS_VULKANPLATFORM=wayland
fi
PROFILE
printf '%s\n' hybris > /etc/proroot-driver
printf '%s\n' 'Installed and selected libhybris. Stop and restart the terminal session,'
printf '%s\n' 'then run: /usr/lib/hybris/bin/test_vulkan   (or: proroot-hybris vulkaninfo --summary)'
printf '%s\n' 'Return to RADV with: proroot-driver radv'
