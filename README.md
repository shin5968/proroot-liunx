# libhybris crash diagnostics test APK

Install this APK over the existing test app, then select libhybris install again to update the guest probe, and run GPU diagnostics. Copy the full result.

The Android 17 system Vulkan load SIGSEGV is unresolved. This build adds bounded fatal-signal memory maps and an explicit failure reason. It does not claim GPU success, Wayland presentation or zero-copy.

Validated: Android build/lint and 22 unit tests, host and ARM64/QEMU fatal-signal tests, actual ARM64 bridge negative probe and guest-bind tests. Device driver testing remains pending.

Binary-only distribution branch; source changes remain in the development workspace and this branch is not a source snapshot.
