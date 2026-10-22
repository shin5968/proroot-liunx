# RADV and Android AHB device validation — 2026-10-22

Device: Samsung Galaxy S24 SM-S921N, Android 17, Xclipse 940, native AMDGPU DRM 3.42.0. Guest: Ubuntu 24.04.5 ARM64 / glibc 2.39. App 0.4.0 was installed as an update; existing guest files and packages were retained.

## Provenance and CI

- [App build](https://github.com/shin5968/proroot-liunx/actions/runs/39760936175): APK, lint, unit tests, touch regression tests, unchanged Proroot runtime byte checks passed.
- [RADV build](https://github.com/shin5968/proroot-liunx/actions/runs/39760936215): selected radv-glibc commit `d29b3afcecc542119bd548308a34e8dead6f5655`, plus the recorded source adaptations.
- Driver ZIP SHA-256: `ea4a405cd06baf95756ef0855c85f9d5c99da9e4cf245ff67690ca4fb72c2348`.
- The build artifact includes the patch and native-DRM source hashes and upstream notices.

This is not a stock upstream binary: opt-in direct-node discovery and native DRM/file-mmap calls were added to RADV. Proroot's five original binaries were not changed.

## Results

| Check | Result |
| --- | --- |
| Explicit ICD selection / vulkaninfo | PASS, exit 0 |
| Physical hardware device | AMD Radeon Graphics (RADV MAGELLAN CARL), vendor 0x144d, device 0x73a0 |
| Driver/API | Mesa RADV 27.0.0-devel / Vulkan 1.4.363 |
| Real VkDevice creation | PASS |
| GPU buffer fill, fence completion, exact diagnostic readback | PASS |
| Linux DMA-BUF export | PASS |
| Android AHB Unix socket-handle roundtrip | PASS, same-process socketpair diagnostic |
| Android Vulkan AHB import / GPU clear | PASS |
| Android SurfaceControl presentation | Green test image visibly presented |
| App Linux verification button | PASS; process_exit=0, fill_verified=true, dma_buf_export=true |
| Native Wayland presentation | NOT TESTED: compositor/bridge not implemented |
| DRI3 / Xwayland presentation | NOT TESTED: X server/Xwayland not implemented |
| End-to-end DRI3/Wayland → AHB zero-copy | NOT VERIFIED |
| Direct scanout / sustained throughput / zero overhead | NOT VERIFIED |

The Linux readback deliberately maps GPU memory for testing. It does not prove zero-copy presentation. The Android host test has no application CPU pixel map or image-copy command, but waits for a GPU fence on the CPU. It is not a production asynchronous presenter.

## Problems isolated

1. The original glibc build could not discover GPUs through denied sysfs paths. Opt-in explicit-node discovery now performs ordinary kernel device checks.
2. The guest's Proroot ioctl path reported `zink / proroot zink bridge GPU` instead of native AMDGPU. Direct kernel queries showed that renderD128 is AMDGPU 3.42; renderD129 is the display controller. RADV's opt-in native DRM calls resolve this without changing Proroot binaries or Android privileges.
3. Command-buffer allocation initially failed until real file-backed BO mmap was routed to the kernel as well.
4. New Wayland protocol XML and Mesa require matching scanner/client versions. Wayland 1.24 is built with the driver; the guest's original client library was backed up before installing the matching one.
5. Android device extensions include both DMA-BUF and AHB, but the tested RGBA8 optimal image query advertised disjoint compatible handle types (512 and 1024). Direct Vulkan handle re-export between these two types is not supported for that configuration. A general claim that all possible vendor-specific routes are impossible would go beyond this test.

No CPU-copy fallback was used to manufacture an end-to-end success. The missing compositor and AHB-aware presentation/allocator integration remain explicit work, described in [the backend design](GRAPHICS_BACKEND.md).

## Evidence

- [Host report](device-tests/v0.4.0/host.json)
- [Linux GPU/DMA-BUF report](device-tests/v0.4.0/linux.json)
- [vulkaninfo summary](device-tests/v0.4.0/vulkaninfo.txt)
- [Device screenshot](device-tests/v0.4.0/graphics.png)

The app is left on the graphics validation screen. The installed guest provides `proroot-radv vulkaninfo --summary` and `proroot-radv proroot-graphics-probe`.
