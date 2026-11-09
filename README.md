# libhybris prototype APK 0.5.1

Install over the prior debug APK. In Wayland or Graphics, choose libhybris Vulkan 진단 -> libhybris 설치 -> GPU 진단. Installation selects libhybris for new Ubuntu guest sessions; no RADV ZIP is required. Use 결과 복사 to share the report.

The bridge loads the phone system Android Vulkan driver through actual libhybris. The diagnostic tests driver loading and GPU buffer fill/readback. Libhybris Wayland presentation and end-to-end zero-copy are NOT implemented/verified; the existing SHM presenter remains separate. Unmodified OEM Android 17 firmware compatibility and GPU work must be checked on the phone. No device firmware is bundled or altered.

libhybris pin: 7079712a42ea2754adf747e70c6cc75764c8596e. Android headers pin: 217a38e92cc64a96b4d1ac7469f30827d84485d3 (Android 11 headers; actual device SDK is passed without downgrading). Q linker/null Vulkan platform; Android 10+ for this component.

Cloud validation: ARM64 build; real Q-loader Android-target ELF fixture repeated load/call/unload with no FD leak; missing-firmware probe fails rather than claiming GPU success; Android build/lint/22 unit tests; signed APK payload/hash and original Proroot byte verification. Fixture is not shipped.

This binary includes uncommitted workspace development; this branch is a binary distribution, not a source snapshot. Notices and pinned upstream source links are bundled.
