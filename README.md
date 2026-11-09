# libhybris launcher bind fix

Fix shipped Proroot launcher rejection: every -b argument now explicitly uses host:guest, e.g. /system:/system. All system path mappings use this form; tests validate the actual argument shape and whitelist.

Install this APK over 0.5.1, then reopen libhybris Vulkan 진단 and run GPU 진단. The installed runtime is unchanged and need not be reinstalled. This fixes a launcher failure before libhybris loading; it does not establish actual Android firmware/GPU compatibility. Wayland output via libhybris remains unconnected.

Build/lint/unit checks and the guest binding regression test passed. APK signature and unchanged runtime/proprietary Proroot bytes verified. This binary distribution includes uncommitted workspace changes, not a complete source snapshot.
