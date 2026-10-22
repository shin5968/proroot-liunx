#!/bin/sh
# Install a GitHub-built bundle INSIDE the ARM64 Ubuntu rootfs.
# This never modifies Android /system or the Proroot runtime.
set -eu
bundle=$(cd "${1:?usage: sh install-radv.sh /path/to/extracted-radv-glibc-arm64-artifact}" && pwd)
test "$(uname -m)" = aarch64
command -v unzip >/dev/null 2>&1 || { printf "%s\n" "Install unzip in the guest first: apt-get install unzip" >&2; exit 1; }
test "$(cat "$bundle/SOURCE_COMMIT")" = d29b3afcecc542119bd548308a34e8dead6f5655
(cd "$bundle" && sha256sum -c SHA256SUMS && sha256sum -c PATCH_SHA256)
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT HUP INT TERM
unzip -q "$bundle/radv-xclipse-glibc-27.0.0-devel-d29b3af.zip" -d "$stage"
package="$stage/radv-xclipse-glibc-27.0.0-devel-d29b3af"
test -f "$package/libvulkan_radeon.so"
mkdir -p /usr/lib/xclipse/wayland /usr/share/vulkan/icd.d /usr/local/bin
install -m 644 "$package/libvulkan_radeon.so" /usr/lib/xclipse/
install -m 644 "$package/radeon_xclipse_icd.aarch64.json" /usr/share/vulkan/icd.d/
install -m 644 "$package/NOTICE.txt" /usr/lib/xclipse/
install -m 644 "$bundle"/wayland/libwayland-client.so.0* /usr/lib/xclipse/wayland/
install -m 644 "$bundle/wayland/LICENSE" /usr/lib/xclipse/wayland/
install -m 644 "$bundle/SOURCE_COMMIT" "$bundle/PATCH_SHA256" /usr/lib/xclipse/
# Proroot's loader did not resolve the newer Wayland library from LD_LIBRARY_PATH
# in this test. Preserve the original before selecting the matching ABI-compatible
# libwayland-client.so.0 in the distribution's normal search directory.
lib=/usr/lib/aarch64-linux-gnu
mkdir -p /root/proroot-graphics-backup
if [ -e "$lib/libwayland-client.so.0" ] && [ ! -e /root/proroot-graphics-backup/libwayland-client.so.0 ]; then
    cp -L "$lib/libwayland-client.so.0" /root/proroot-graphics-backup/
fi
install -m 644 "$bundle/wayland/libwayland-client.so.0.24.0" "$lib/"
ln -sfn libwayland-client.so.0.24.0 "$lib/libwayland-client.so.0"
if [ -f "$bundle/proroot-graphics-probe" ]; then
    install -m 755 "$bundle/proroot-graphics-probe" /usr/local/bin/
fi
cat > /usr/local/bin/proroot-radv <<'WRAPPER'
#!/bin/sh
export VK_DRIVER_FILES=/usr/share/vulkan/icd.d/radeon_xclipse_icd.aarch64.json
export VK_ICD_FILENAMES="$VK_DRIVER_FILES"
export PROROOT_DRM_RENDER_NODE=/dev/dri/renderD128
export PROROOT_NATIVE_DRM=1
if [ "$#" = 0 ]; then set -- vulkaninfo --summary; fi
exec "$@"
WRAPPER
chmod 755 /usr/local/bin/proroot-radv
printf '%s\n' 'Installed. Run: proroot-radv vulkaninfo --summary'
printf '%s\n' 'Presentation and end-to-end zero-copy must be validated separately.'
