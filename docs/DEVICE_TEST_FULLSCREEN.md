# Fullscreen terminal device test — 2026-10-20

App: 0.3.0, source `9589ca2`. Device: Samsung Galaxy S24 (SM-S921N), Android 17, ARM64.

GitHub Actions build, lint, unit tests and bundled runtime integrity checks passed:
https://github.com/shin5968/proroot-liunx/actions/runs/39409877860

The final APK was installed with `adb install -r`; the existing Ubuntu rootfs and installed packages were retained.

| Check | Result |
| --- | --- |
| Native mint home, container, desktop preferences and settings | PASS |
| Fullscreen terminal without app navigation or system bars | PASS |
| Display cutout area | PASS: viewport 384 × 832 CSS pixels, 51 rows × 42 columns |
| Real Proroot guest commands | PASS: glibc 2.39, aarch64, hello output |
| Home → terminal session retention | PASS: same guest shell PID |
| Optional Ctrl-C controls | PASS: running sleep interrupted; next command succeeded |
| Overflow keyboard action | PASS on final APK: FULLSCREEN_KEYBOARD_OK entered through Android keyboard |
| Keyboard viewport resize | PASS: 29 terminal rows with keyboard visible |

The session retention, Ctrl-C and full viewport checks were performed on the preceding UI candidates; the final change only delays keyboard focus until the overflow popup closes. The final APK was installed and its keyboard action and terminal input were retested.

Screenshots: [home](device-tests/v0.3.0/home.png), [fullscreen terminal](device-tests/v0.3.0/terminal.png), [final keyboard test](device-tests/v0.3.0/keyboard.png).

Returning home preserves the session while the activity lives. Activity destruction stops it; a foreground service is not implemented. Desktop options currently save preferences; libhybris/Vulkan GUI rendering remains unimplemented. These checks establish functionality, not a zero-overhead performance claim.
