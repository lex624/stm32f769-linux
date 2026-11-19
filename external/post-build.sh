#!/bin/sh
#
# Post-build tweaks for the STM32F769I-DISCO.
#
# Buildroot's busybox package installs syslogd/klogd init scripts unconditionally,
# but this target builds busybox without those applets. Left in place they each
# cost a failed exec at boot, and on noMMU every exec needs one physically
# contiguous allocation the size of the whole binary (~352 KB here) - precisely
# when memory is tightest and the network is coming up.
set -e
TARGET="$1"

rm -f "$TARGET/etc/init.d/S01syslogd" "$TARGET/etc/init.d/S02klogd"

# Same story for seedrng: the applet is not built, so the script would only burn
# a failed exec at boot.
rm -f "$TARGET/etc/init.d/S01seedrng"

# Both Linux consoles receive printk. The last console remains the serial input.
sed -i 's/console=ttySTM0/console=tty0 fbcon=rotate:1 clk_ignore_unused console=ttySTM0/' "$TARGET/boot/extlinux/extlinux.conf"

# The root-level script runs before U-Boot scans /boot for extlinux.conf.
"$HOST_DIR/bin/mkimage" -A arm -O linux -T script -C none -n F769 \
    -d "$BR2_EXTERNAL_STM32F769_PATH/f769-boot.cmd" "$TARGET/boot.scr"
