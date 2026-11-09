# libhybris loader trace test APK

Install this APK over the existing test app, then select libhybris install again to update the guest probe, and run GPU diagnostics. Copy the full result.

The Android 17 system Vulkan load SIGSEGV is unresolved. This build reopens thread-self/maps at the fault, records glibc module load biases, separates Q linker initialization, and enables upstream dependency constructor tracing. It does not claim GPU success, Wayland presentation or zero-copy.

Validated: Android build/lint and 22 unit tests, host and ARM64/QEMU fatal-signal tests including late mappings, a real-Q-linker Android constructor-crash fixture, actual ARM64 bridge negative probe and guest-bind tests. Device driver testing remains pending.

Binary-only distribution branch; source changes remain in the development workspace and this branch is not a source snapshot.
