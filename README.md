# Proroot Linux — Android host MVP

ARM64 Android host built with Jetpack Compose, offline xterm.js 5.5.0 + fit addon 0.10.0, and a native PTY bridge.

## Included

- Terminal / GUI / Container tabs, real PTY input, output, resize and process termination.
- Host shell smoke test (`/system/bin/sh`). This is Android, not a Linux rootfs.
- Official Proroot launcher integration with rootfs and private temp directory.
- SurfaceView placeholder. Vulkan ICD, libhybris, WSI, audio, rootfs download/import, multiple containers and background service are **not implemented**.
- ARM64 debug APK + lint reports built by GitHub Actions on push, PR or manual dispatch.

## Build

GitHub → Actions → Android APK → successful run → Artifacts → `proroot-linux-arm64-debug`.
Unzip and install the APK on an ARM64 Android 8+ device. Use **Host shell** to test terminal I/O (`echo hello`, `ls`, `stty size`), Ctrl-C and Stop/restart.
A local build needs JDK 17, Android SDK 35, NDK 27.2.12479018 and CMake 3.22.1: `./gradlew :app:assembleDebug :app:lintDebug`.

## Enable Linux (developer provisioning)

Proroot binaries and Ubuntu rootfs are deliberately not redistributed in this initial source tree.
Obtain the five unmodified ARM64 libraries from https://github.com/coderredlab/proroot/releases and review its LICENSE before redistribution. Place them in `app/src/main/jniLibs/arm64-v8a/` before building:

- libproroot.so
- libproroot-runtime.so
- libproroot-linker.so
- libproroot-bridge.so
- libproroot-stub-loader.so

The `.gitignore` excludes these binaries. GitHub builds from the current repository provide the host shell; adding binaries to a local directory alone does not put them into a GitHub build.

Install the debug APK first. Provision a **trusted** Ubuntu ARM64 rootfs archive that contains `bin/sh` and `/root` directly under its root (not inside an extra directory). On an adb-connected device:

```sh
adb push ubuntu-arm64-rootfs.tar.gz /data/local/tmp/proroot-rootfs.tar.gz
adb shell "run-as dev.proroot.app mkdir -p files/rootfs"
adb shell "run-as dev.proroot.app /system/bin/tar -xzf /data/local/tmp/proroot-rootfs.tar.gz -C files/rootfs"
adb shell "run-as dev.proroot.app mkdir -p files/rootfs/root"
```

Device tar support, ownership handling and symlink behavior must be checked on the target device. This is a developer procedure, not a finished installer. Open Container to inspect paths, then Linux to launch `/bin/sh -i` through `libproroot.so`.

## Compatibility and lifecycle

This prototype targets SDK 28 because Android's newer app-data execution policy must be addressed before raising the target. It is not ready for Play Store distribution. The target must not be raised without validating the glibc loader/rootfs execution route. Modern SDK targeting is a separate integration task.

Runtime runs with the app UID. Fake root does not grant device permissions or isolation. Run only trusted programs. Closing the activity stops its session; background persistence is not implemented. Rotation preserves the activity. GUI tab currently contains an empty SurfaceView and cannot display Linux windows.

## Next gate

On one specific device, first establish rootless Proroot execution, then demonstrate Vulkan GPU enumeration and compute via a libhybris ICD in the same process. Verify loader/TLS coexistence, vendor dependencies and SELinux access before integrating WSI and shared buffers. Never treat the empty SurfaceView as GPU acceleration.

## Third-party notices

xterm.js and addon-fit are MIT licensed; notices are included beside the bundled assets. Sources: https://github.com/xtermjs/xterm.js . No Termux source is included. Gradle wrapper comes from Gradle 8.9 (Apache-2.0): https://github.com/gradle/gradle . Proroot remains a separately supplied proprietary dependency. No zero-overhead performance claim has been measured.
