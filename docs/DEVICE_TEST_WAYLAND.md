# Wayland Surface output — 2026-10-23

Tested app 0.5.0 (versionCode 12), source `afcce7aa6485325405da9636a1c585c6c4c459f8`, on Samsung Galaxy S24 SM-S921N / Android 17 / ARM64 with the existing Ubuntu Base 24.04.5 rootfs. Installed using `adb install -r`; rootfs and user data were preserved.

GitHub build: https://github.com/shin5968/proroot-liunx/actions/runs/39950589367 — all jobs passed: native ARM64 Wayland runtime/output tests, terminal touch regression, APK build/lint/rootfs unit tests, and unmodified Proroot binary verification.

Installed APK SHA-256: `6b84e5848be3597734f4422d0f21f4ce0d9b1bb4d8962678e517be1231c7bd37`.

## Results

- **Desktop → Wayland 화면 실행:** real Linux xdg-shell client connected to the guest compositor; animated shared-memory pixels appeared in Android SurfaceView. The saved portrait report records 742 posted frames at 720×1110 without a presenter error. This is a frame counter, not a measured frame rate.
- **Restart:** animation resumed with a fresh frame counter. Previous session processes disappeared; only the new launcher/Bash/compositor/demo tree remained.
- **Rotation:** landscape output reached 720×136 and returned to portrait 720×1110 with advancing counters. The original `wm user-rotation lock 0` setting was restored. Screenshots show both orientations.
- **Home/background:** all compositor-session processes disappeared. No orphaned Bash or compositor/demo remained. Reopening the Wayland screen started another working session. Direct restoration of the same Activity from Recents was not separately verified.
- **Close with an independent terminal active:** terminal Bash PID 4374 remained the same and executed a new command after the Wayland session closed. Only that terminal's launcher and Bash remained. The Wayland cleanup did not terminate the independent terminal.
- No AndroidRuntime/libc fatal messages appeared in the inspected logcat tail.

The first device build exposed orphaned Proroot Bash children spinning after group-only termination and unnecessary surface restarts from changing status-text height. The tested final source freezes and terminates only the launcher's actual descendant tree across process groups, and reserves fixed height for status text. The orphan processes from that initial test were explicitly removed before final verification.

## Evidence and limits

See [portrait](device-tests/v0.5.0/wayland.png), [landscape](device-tests/v0.5.0/landscape.png), and [presenter report](device-tests/v0.5.0/wayland.json).

This is the first Wayland output stage: `wl_shm → sealed snapshot FD → Android CPU conversion/scaling → ANativeWindow`. It uses two CPU pixel copies. It does not establish RADV-rendered GUI frames, DMA-BUF/AHB zero-copy, compositor input, Xwayland/DRI3, or a general desktop environment. The report retains `end_to_end_zero_copy: NOT_VERIFIED`.
