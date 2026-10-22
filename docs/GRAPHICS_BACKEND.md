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
proroot-radv vulkaninfo --summary
proroot-radv /usr/local/bin/proroot-graphics-probe
```

The installer is `scripts/graphics/install-radv.sh`. The wrapper explicitly selects this ICD; it does not silently substitute lavapipe. The guest must have the Vulkan loader, XCB/X11 client libraries, libelf, libxshmfence, libstdc++ and libffi. Install missing dependencies from the distribution's package manager.

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

The current implementation contains the driver build/integration probes and the Android AHB presenter test. A Wayland compositor, Xwayland server and full application presentation bridge are NOT implemented yet. Desktop entries remain marked as preparing. Do not interpret the test rectangle as a Linux desktop.

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
