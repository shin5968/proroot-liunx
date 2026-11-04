# RADV DMA-BUF diagnostic test APK

Install over the previous debug APK, open Desktop -> Wayland -> RADV DMA-BUF 진단. Linux RADV and Vulkan loader must already be installed in the rootfs. Use 결과 복사 to share the result.

The diagnostic queries Android import candidates, intersects them with RADV export capabilities and attempts 120 changing RGB frames via explicit acquisition and completion replies. It does not fall back to CPU output. Hardware import/display has NOT been verified in cloud tests. Zero-copy remains NOT_VERIFIED.

Host checks: Android build/lint/17 unit tests, selection/transport/endpoint tests, software Vulkan pixel + synchronization validation (0 errors), ARM64 QEMU startup/argument checks, APK signature and bundled byte checks.

This APK includes uncommitted workspace development changes; this download branch is a binary distribution, not a complete source snapshot.
