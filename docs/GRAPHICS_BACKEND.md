# RADV / Android Surface / Wayland backend

## Selected source and build

The requested driver is [JimVulkan/radv-xclipse, radv-glibc](https://github.com/JimVulkan/radv-xclipse/tree/radv-glibc), pinned to `d29b3afcecc542119bd548308a34e8dead6f5655` (Mesa 27.0.0-devel).

GitHub Actions builds ARM64 glibc binaries on Ubuntu 24.04 ARM, matching the guest's glibc 2.39. The driver requires at least glibc 2.38 in this build. Wayland 1.24.0 tools/runtime are pinned to `736d12ac67c20c60dc406dc49bb06be878501f86`; the newer protocol XML requires matching scanner and client-library versions.

The distribution's original Wayland client is backed up before selecting the matching newer library. Android system libraries are never replaced. The original Proroot v1.2.8 binaries remain unchanged.

Our small driver adaptations are recorded alongside the binary:
- opt-in render-node discovery when Android denies sysfs enumeration;
- opt-in native DRM open/ioctl and file-backed mmap calls, because Proroot otherwise presents a virtual zink DRM device;
- `PROROOT_NATIVE_DRM=1` enables this path; kernel permissions and SELinux still apply.

The build artifact contains the source commit, patch/source hashes, original upstream notices, Wayland license and ZIP SHA-256. This is an adapted build of the selected branch, not an unmodified upstream release.

## Installation and use

Download and extract the `radv-glibc-arm64` artifact from **RADV glibc ARM64**, transfer it to the Ubuntu guest, then run:

```sh
sh install-radv.sh /path/to/extracted-artifact
. /etc/profile.d/proroot-radv.sh # current shell; new app sessions select RADV automatically
vulkaninfo --summary
/usr/local/bin/proroot-graphics-probe
```

The installer is `scripts/graphics/install-radv.sh`. App 0.4.1 and later select the installed Xclipse ICD and native DRM path for every new guest PTY, including the graphics diagnostic. Selection requires both the driver library and ICD file; fresh rootfs sessions and the Android diagnostic shell retain their normal environment. Child processes inherit these defaults. `/etc/profile.d/proroot-radv.sh` also supplies defaults to login shells after installation. The `proroot-radv` wrapper remains available for compatibility. The guest must have the Vulkan loader, XCB/X11 client libraries, libelf, libxshmfence, libstdc++ and libffi. Install missing dependencies from the distribution's package manager.

The graphics screen in the Android app separately runs:
- **Android host**: allocate AHB, transfer its handle through a Unix socket pair, import into Vulkan, GPU-clear the image, release ownership, wait for the test fence and submit that same AHB through SurfaceControl;
- **Linux RADV**: create a real device, submit a GPU buffer fill, verify diagnostic readback and export a DMA-BUF.

The readback is a diagnostic CPU read. It is not a zero-copy presentation test. The host test waits on the CPU for its fence; asynchronous acquire/release fence handling remains necessary for a production presenter. No claim about frame rate, overhead, hardware overlay assignment or direct scanout follows from these tests.

## Presentation architecture

DRI3 belongs to X11. For Wayland, the corresponding buffer-sharing route is `zwp_linux_dmabuf_v1`. They are separate protocol stages:

```text
Wayland application -> RADV Wayland WSI -> Wayland compositor
X11 application -> DRI3/Present -> Xwayland -> Wayland compositor
Wayland compositor / Android presenter -> AHB -> SurfaceControl -> SurfaceFlinger
```

App 0.5.0 adds an app-managed minimal Wayland compositor and Android Surface presenter. Open **데스크톱 → Wayland 화면 실행**. The bundled Linux client creates an xdg_toplevel and animated wl_shm buffers; these are actually submitted over the Wayland socket and displayed in SurfaceView. This is a single fullscreen output bootstrap, not XFCE/GNOME/KDE or a general desktop shell. Keyboard/touch input, subsurfaces, popups, GPU buffers and Xwayland remain future work.

The compositor is a glibc ARM64 process inside Ubuntu, built with statically linked Wayland 1.24.0 and libffi. Android keeps its Bionic libraries separate. GitHub Actions builds the guest executables before packaging the APK; the rootfs still downloads separately. Wayland/libffi/protocol licenses and runtime hashes are included in `assets/wayland/`. The downloaded Ubuntu 24.04 rootfs supplies glibc. `scripts/wayland/build.sh` also supports a native Linux build for testing.

Current output path:

```text
Linux demo -> wl_shm / xdg-shell -> guest compositor
 -> CPU snapshot into sealed memfd -> Unix SCM_RIGHTS FD + frame metadata
 -> Android mmap -> CPU pixel conversion/scaling -> ANativeWindow -> SurfaceView
```

The presenter checks same-UID peers, dimensions, stride, file size and seals before mapping. The compositor waits for a sequence ACK after Android queues the frame before releasing the client's wl_buffer. This is synchronous bootstrap backpressure, not asynchronous presentation-fence support. There are two CPU pixel copies; **this path is not zero-copy and does not validate RADV GUI presentation**. `wayland-report.json` explicitly reports `WL_SHM_CPU_COPY` and `NOT_VERIFIED`.

The Wayland socket is `/tmp/proroot-wayland/wayland-proroot` inside the guest (`XDG_RUNTIME_DIR=/tmp/proroot-wayland`, `WAYLAND_DISPLAY=wayland-proroot`). The Android bridge socket is private under the same app's rootfs `/tmp/proroot-present.sock`. The activity stops its PTY process group and presenter on pause/surface destruction and starts a fresh session on resume; a persistent desktop foreground service is not yet provided. Restart and surface recreation reset the compositor and animation. Logs and counters are kept at app-private `files/wayland-session.log` and `files/wayland-report.json`.

CI exercises real Wayland configure/ACK, buffer commits, frame callbacks, releases, sealed FD transport, changing pixel contents and restarting with the same socket name. Device testing remains necessary for Android presentation and Proroot behavior.

The selected glibc driver disables Android AHB support at compile time (`radv_android.h`); it provides DMA-BUF external memory instead. Device extension names alone do not prove interoperability with Android AHB.

On the tested S24 system driver, RGBA8 optimal images reported:
- DMA-BUF handle `512`: compatible handles `512`, export-from-imported `512`;
- AHB handle `1024`: compatible handles `1024`, export-from-imported `1024`.

Thus direct DMA-BUF-to-AHB re-export is not advertised for the tested image configuration. This is not proof that every vendor-specific allocation route is impossible. An AHB-aware allocation/WSI integration, carrying the complete native-handle/plane/modifier metadata and fences, needs implementation and real validation. A GPU blit into another AHB would be a different, non-zero-copy presentation path and is not silently used here.

## Required end-to-end evidence

Approve zero-copy only after all of the following are observed for real application frames:
1. The selected device is hardware RADV on Xclipse, not lavapipe or a virtual zink device.
2. DRI3/Present is active for X11/Xwayland clients; native Wayland advertises and actually uses linux-dmabuf.
3. Producer allocation, FD transport and Android consumer refer to the same compatible allocation, with correct format, planes, offsets, stride and modifier.
4. Acquire and release fences protect reuse; no premature wl_buffer.release or FD closure occurs.
5. No wl_shm/PutImage fallback, CPU pixel copy or presentation blit is used.
6. The same frames are visibly presented and sustained under resize, occlusion, surface recreation and app pause/resume.

Current reports intentionally retain `end_to_end_zero_copy: NOT_VERIFIED` until that evidence exists.
