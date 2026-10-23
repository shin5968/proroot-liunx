# Proroot Linux — Android host MVP

ARM64 Android host with Compose, offline xterm.js, native PTY, bundled Proroot and an in-app Ubuntu installer.

## Install and launch

1. GitHub → Actions → Android APK → successful run → Artifacts → `proroot-linux-arm64-debug`.
2. Unzip and install the APK on an ARM64 Android 8+ device.
3. Open **홈 → Ubuntu 설치하고 시작** (or the Ubuntu container card). Keep the app open during installation. Internet and at least 1.5 GiB free space are required.
4. After verification and extraction, press **터미널 시작**. The terminal uses the full screen with system bars and app navigation hidden. Tap **⋮** for keyboard, optional Ctrl/arrow keys, font sizing, stop/restart and returning home while keeping the session. Android diagnostic shell is in Settings.
5. Open **데스크톱 → Wayland 화면 실행** for the Linux Wayland animation on Android Surface. This first output path uses wl_shm and CPU copies; it is not the GPU/zero-copy path.

The APK includes all five unmodified Proroot v1.2.8 libraries. CI fetches them from the official release and checks pinned SHA-256 values, then verifies the APK contains the identical bytes. Binaries are not redistributed as standalone repository files. The license is bundled in assets and available in Settings.

Ubuntu Base 24.04.5 ARM64 is downloaded separately from the official Ubuntu server:
https://cdimage.ubuntu.com/ubuntu-base/releases/24.04/release/ubuntu-base-24.04.5-base-arm64.tar.gz

Pinned SHA-256: `a91d5a93010193712d346d761372b7c9db6dfcf093893161c64ca107f05914f2` (official SHA256SUMS).

Installation uses a private staging directory, verifies the download before extraction, preserves symlinks and executable modes (archive hardlinks fall back to copies when Android denies link creation), rejects archive path traversal, then renames the completed tree into place. Cancel/failure cleans the temporary download and staging tree without following symlinks. A previous rootfs is never overwritten. Failed downloads restart from zero on retry. An activity recreation retains the installer via ViewModel; process death requires retry. Persistent background downloading is not implemented.

## Development

JDK 17, SDK 35, NDK 27.2.12479018 and CMake 3.22.1 are required.

```sh
bash scripts/fetch-proroot.sh
# On a native ARM64 Ubuntu builder with Meson, Ninja, libffi-dev and libexpat1-dev:
bash scripts/wayland/build.sh "$PWD/app/src/main/assets/wayland"
./gradlew :app:assembleDebug :app:lintDebug :app:testDebugUnitTest
```

GitHub Actions also tests extraction of the actual pinned Ubuntu archive, malicious archive paths, link handling and cancellation. It uploads the APK and lint report. Device-side Linux execution is a separate validation step.

## Current scope

Terminal input/output/resize, Ctrl-C/Tab/Esc, session termination, container install/progress/cancel/retry, Proroot launcher integration and the mint mobile UI are implemented. A pinned, adapted radv-glibc build and Vulkan/AHB diagnostics are available; see [graphics setup and validation](docs/GRAPHICS_BACKEND.md). The driver is a separate GitHub artifact installed into the downloaded rootfs, not bundled in the APK. A bundled minimal Wayland compositor displays a real Linux wl_shm client in SurfaceView using CPU copies. Desktop preferences still only save XFCE/GNOME/KDE choices. A general desktop shell, GPU Wayland presentation, Xwayland, zero-copy GUI, libhybris, audio, multiple containers and a foreground service are not implemented. Returning home keeps the terminal PTY alive; the Wayland activity stops its compositor on pause. Destroying the activity stops terminal sessions; downloads survive activity recreation but not process death.

SDK 28 is intentional for prototype rootfs execution compatibility. The Play Store target requirement remains a lint warning, and this APK is for direct installation. Raising the target requires validating a different app-data execution route. Proroot fake root does not grant device privileges; guest apps run with the host app UID. No zero-overhead performance result has been measured.

## Third-party notices

- Proroot v1.2.8 by coderred: https://github.com/coderredlab/proroot . Unmodified binaries bundled only as part of the APK under the license in `app/src/main/assets/licenses/proroot-LICENSE.txt`.
- xterm.js 5.5.0 / addon-fit 0.10.0: MIT, licenses beside assets. https://github.com/xtermjs/xterm.js . No Termux source is included.
- Apache Commons Compress: Apache-2.0. https://commons.apache.org/proper/commons-compress/ .
- Gradle wrapper 8.9: Apache-2.0, license in `third-party/gradle-LICENSE`.
- RADV Xclipse glibc: Mesa/MIT and component licenses, original NOTICE.txt in the driver artifact; [source and patches](docs/GRAPHICS_BACKEND.md).
- Wayland 1.24 / xdg-shell protocol: MIT, notices included in the bundled Wayland assets and separate runtime artifact. Statically linked libffi includes its upstream license notice in those assets.
- Ubuntu Base: https://www.ubuntu.com/legal . Per-package licenses are in the downloaded rootfs `/usr/share/doc`.


Debug APKs use the checked-in public development keystore so successive GitHub builds can update the test installation. This key is not suitable for production signing. Older builds signed with ephemeral keys require a one-time reinstall.
