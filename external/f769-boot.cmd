# The compressed kernel relocates itself across the original DTB address.
# Keep the tree in upper SDRAM until Linux has parsed it.
setenv fdt_addr_r 0xc0a00000
setenv fdt_high 0xffffffff
sysboot mmc 0:1 any ${pxefile_addr_r} /boot/extlinux/extlinux.conf
