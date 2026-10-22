from pathlib import Path
import shutil,sys
root=Path(sys.argv[1]);source=Path(sys.argv[2])
target=root/'src/amd/vulkan'
shutil.copyfile(source/'proroot-native-drm.c',target/'proroot_native_drm.c')
p=target/'meson.build';s=p.read_text()
needle="libradv_files = files("
assert needle in s
s=s.replace(needle,needle+"\n  'proroot_native_drm.c',",1)
needle='ld_args_build_id, ld_args_bsymbolic, ld_args_gc_sections, vulkan_icd_link_args,'
assert needle in s
s=s.replace(needle,needle+"\n    '-Wl,--wrap=open,--wrap=open64,--wrap=ioctl',",1)
p.write_text(s)
