# libhybris saved crash symbol analysis APK

Install this APK over the existing test app. Open libhybris and press 저장된 충돌 분석, then 결과 복사. Do not reinstall the guest runtime or rerun GPU diagnostics first: this action reads the previously saved crash report and exact device ELF files.

The Android 17 Vulkan loading SIGSEGV remains unresolved. The last device trace faults inside Bionic libc at ELF offset0x8809c, from the third libc++ constructor. This update resolves exported function names using the device files without loading their code. It reports unknown symbols explicitly, handles PAC return addresses, and accounts for differing ELF virtual/file offsets. Original saved diagnostics and all guest assets are preserved.

Validated: Android build/lint and25unit tests including actual packaged ARM64 Q linker symbol lookup, PAC-tagged addresses, differing segment offsets and malformed/missing ELF inputs. No device compatibility fix or GPU/Wayland/zero-copy success is claimed.

Binary-only distribution branch; development source changes remain uncommitted in the workspace.
