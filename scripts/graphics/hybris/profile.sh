# Sorts after proroot-radv.sh. libhybris is used unless radv or none is selected.
case "$(cat /etc/proroot-driver 2>/dev/null)" in
    radv|none) ;;
    *) if [ -r /usr/lib/hybris/lib/libhybris/linker/q.so ]; then
        unset VK_DRIVER_FILES VK_ICD_FILENAMES PROROOT_NATIVE_DRM PROROOT_DRM_RENDER_NODE
        case ":${LD_LIBRARY_PATH:-}:" in
            *:/usr/lib/hybris/lib:*) ;;
            *) export LD_LIBRARY_PATH=/usr/lib/hybris/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH} ;;
        esac
        export HYBRIS_EGLPLATFORM=wayland HYBRIS_VULKANPLATFORM=wayland
    fi ;;
esac
