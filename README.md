# libhybris MB_CUR_MAX compatibility fix APK

Update the APK, open libhybris, press libhybris 설치 again to install the changed guest runtime, then run GPU 진단 and copy the result.

The device identified the failing symbol as Bionic __ctype_get_mb_cur_max+12, called during libc++ locale initialization. This build routes it through glibc to match the existing locale hooks. The local patch and SHA256 are recorded separately from the upstream base commit in runtime.properties and NOTICE. Android firmware and the original Proroot binaries are unchanged.

Validation: Android build/lint and25unit tests; actual ARM64 Q linker loads an Android constructor fixture that switches C/C.UTF-8 locales and queries MB_CUR_MAX over32cycles without FD leaks. The previous runtime fails this fixture due to the missing hook. Negative firmware and fatal constructor diagnostics still pass. This is not a verified device Vulkan/GPU/Wayland/zero-copy success; device retesting is required.

Binary-only distribution branch. Development source changes remain uncommitted in the workspace.
