# Sorts after proroot-radv.sh. libhybris is used only when explicitly selected.
if [ "$(cat /etc/proroot-driver 2>/dev/null)" = hybris ] && [ -r /usr/lib/hybris/lib/libhybris/linker/q.so ]; then
    unset VK_DRIVER_FILES VK_ICD_FILENAMES PROROOT_NATIVE_DRM PROROOT_DRM_RENDER_NODE
    export HYBRIS_EGLPLATFORM=wayland HYBRIS_VULKANPLATFORM=wayland
fi
