# Physical device test — 2026-10-19

Device: Samsung Galaxy S24 (SM-S921N), Android 17, ARM64, 4096-byte kernel pages.
Tested application: 0.2.1, source 0c737f4. Final APK built by GitHub Actions:
https://github.com/shin5968/proroot-liunx/actions/runs/39255598312

## Results

| Check | Result |
| --- | --- |
| APK installation and launch | PASS |
| Offline terminal assets | PASS |
| Android PTY output and keyboard input | PASS: PTY_SMOKE_OK, KEYBOARD_OK |
| PTY sizing | PASS: 33 rows x 43 columns on test viewport |
| Ctrl-C interrupt | PASS: sleep interrupted, subsequent command succeeded |
| Bundled Proroot v1.2.8 | PASS: five original binaries included; CI verifies hashes |
| In-app Ubuntu download and checksum verification | PASS |
| Rootfs extraction and installation marker | PASS after hardlink permission fallback fix |
| Linux identity | Ubuntu 24.04.5 LTS, aarch64, glibc 2.39 |
| Fake root identity | uid=0, gid=0 (does not grant Android root) |
| Child shell and file I/O | PASS: CHILD_PROCESS_OK, FILE_IO_OK |
| Guest DNS resolution | PASS: getent ahostsv4 ports.ubuntu.com |
| apt-get update | PASS: exit 0, 37.4 MB fetched |
| apt-get install hello | PASS: exit 0, hello prints Hello, world! |
| Force-stop and relaunch | PASS: rootfs, test file and installed hello retained |
| Update installation with stable debug key | PASS: adb install -r, data retained |
| Terminal / Container tab switching | PASS: terminal viewport returns to nonzero height |
| Stop / restart Linux session | PASS: hello runs after restart |
| Fatal app log after final smoke | No AndroidRuntime error or fatal libc output observed |

## Problems found and fixed

1. Android denied hardlink creation with EACCES during rootfs extraction. The installer now copies a hardlink target when EACCES, EPERM or EXDEV prevents the link, preserving its executable mode.
2. Android WebView computed the percentage-height terminal container as zero. The terminal now uses a fixed viewport container and viewport height.
3. Ephemeral GitHub debug signing keys prevented APK updates. Debug builds now use a checked-in, public development key. This key is unsuitable for production signing.

The first version was not already installed on this device before testing. Test-created installations were reset while changing signing keys; their initial small files directory was backed up locally under `.tools`. The final GitHub APK was successfully installed as an update, preserving the downloaded rootfs. The app is left installed with Ubuntu and the hello package.

## Remaining limitations

Android displays a 16KB ELF alignment compatibility warning on launch. This device uses 4KB pages and completed the tests; native 16KB page compatibility has not been established. The bundled proprietary Proroot binaries were kept unmodified. Vulkan/libhybris/WSI and performance overhead were not tested because those features are not implemented. Download cancellation and interrupted-process recovery were not exercised on the physical device in this run.

Evidence: [terminal screenshot](device-tests/galaxy-s24-linux.png), [apt update log](device-tests/apt-update.txt), [hello installation log](device-tests/apt-install-hello.txt).
