# Proroot Linux — Android host MVP

ARM64 Android host with Compose, offline xterm.js, native PTY, bundled Proroot and an in-app Ubuntu installer.

## Install and launch

1. GitHub → Actions → Android APK → successful run → Artifacts → `proroot-linux-arm64-debug`.
2. Unzip and install the APK on an ARM64 Android 8+ device.
3. Open **Container → Download & install Ubuntu**. Keep the app open during installation. Internet and at least 1.5 GiB free space are required.
4. After verification and extraction, press **Launch Linux**. Host shell remains available to test the Android PTY independently.

The APK includes all five unmodified Proroot v1.2.8 libraries. CI fetches them from the official release and checks pinned SHA-256 values, then verifies the APK contains the identical bytes. Binaries are not redistributed as standalone repository files. The license is bundled in assets and available in the Container tab.

Ubuntu Base 24.04.5 ARM64 is downloaded separately from the official Ubuntu server:
https://cdimage.ubuntu.com/ubuntu-base/releases/24.04/release/ubuntu-base-24.04.5-base-arm64.tar.gz

Pinned SHA-256: `a91d5a93010193712d346d761372b7c9db6dfcf093893161c64ca107f05914f2` (official SHA256SUMS).

Installation uses a private staging directory, verifies the download before extraction, preserves symlinks and executable modes (archive hardlinks fall back to copies when Android denies link creation), rejects archive path traversal, then renames the completed tree into place. Cancel/failure cleans the temporary download and staging tree without following symlinks. A previous rootfs is never overwritten. Failed downloads restart from zero on retry. An activity recreation retains the installer via ViewModel; process death requires retry. Persistent background downloading is not implemented.

## Development

JDK 17, SDK 35, NDK 27.2.12479018 and CMake 3.22.1 are required.

```sh
bash scripts/fetch-proroot.sh
./gradlew :app:assembleDebug :app:lintDebug :app:testDebugUnitTest
```

GitHub Actions also tests extraction of the actual pinned Ubuntu archive, malicious archive paths, link handling and cancellation. It uploads the APK and lint report. Device-side Linux execution is a separate validation step.

## Current scope

Terminal input/output/resize, Ctrl-C/Tab/Esc, session termination, container install/progress/cancel/retry, Proroot launcher integration and GUI SurfaceView placeholder are implemented. Vulkan ICD, libhybris, WSI, audio, multiple containers and a foreground service are not implemented. Closing the activity stops terminal sessions; downloads survive activity recreation but not process death.

SDK 28 is intentional for prototype rootfs execution compatibility. The Play Store target requirement remains a lint warning, and this APK is for direct installation. Raising the target requires validating a different app-data execution route. Proroot fake root does not grant device privileges; guest apps run with the host app UID. No zero-overhead performance result has been measured.

## Third-party notices

- Proroot v1.2.8 by coderred: https://github.com/coderredlab/proroot . Unmodified binaries bundled only as part of the APK under the license in `app/src/main/assets/licenses/proroot-LICENSE.txt`.
- xterm.js 5.5.0 / addon-fit 0.10.0: MIT, licenses beside assets. https://github.com/xtermjs/xterm.js . No Termux source is included.
- Apache Commons Compress: Apache-2.0. https://commons.apache.org/proper/commons-compress/ .
- Gradle wrapper 8.9: Apache-2.0, license in `third-party/gradle-LICENSE`.
- Ubuntu Base: https://www.ubuntu.com/legal . Per-package licenses are in the downloaded rootfs `/usr/share/doc`.


Debug APKs use the checked-in public development keystore so successive GitHub builds can update the test installation. This key is not suitable for production signing. Older builds signed with ephemeral keys require a one-time reinstall.
