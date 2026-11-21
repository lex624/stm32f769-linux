#!/bin/sh
# Builds /bin/lsh: a second, minimal BusyBox used as root's login shell.
#
# Why: on this noMMU board every exec needs one contiguous block for the whole
# flat image. The rootfs BusyBox needs 245 KB (an order-6, 256 KB block), and
# an SSH login holds two at once (dropbear plus the shell). Once
# wpa_supplicant is resident there is usually only one such block. lsh is
# hush plus file tools, linked without uClibc's thread manager, so its image
# (about 110 KB) fits in a 128 KB block, of which the board has a dozen free.
# With
# FEATURE_SH_STANDALONE its own applets re-exec lsh, not the 245 KB BusyBox.
#
# Usage: bench/build-lsh.sh <buildroot-dir> <out-file>
# Uses the BusyBox tarball Buildroot already downloaded and its toolchain.
set -e
BR=${1:?buildroot dir}
OUT=${2:?output file}
HERE=$(cd "$(dirname "$0")" && pwd)
VER=1.38.0
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
tar -xjf "$BR/dl/busybox/busybox-$VER.tar.bz2" -C "$WORK"
cd "$WORK/busybox-$VER"
cp "$HERE/lsh-busybox.config" .config
# Single-threaded errno/raise/nanosleep: keeps uClibc's linuxthreads manager
# (about 35 KB of code, data and bss) out of the link. See lsh-errno.c.
cp "$HERE/lsh-errno.c" libbb/lsh_errno.c
echo 'lib-y += lsh_errno.o' >> libbb/Kbuild.src
export PATH="$BR/output/host/bin:$PATH"
yes "" | make oldconfig > /dev/null
make -j"$(nproc)" CROSS_COMPILE=arm-linux- SKIP_STRIP=y \
    EXTRA_CFLAGS="-Os -mthumb -mcpu=cortex-m7" \
    busybox > build.log 2>&1 ||
    { tail -30 build.log; exit 1; }
cp busybox "$OUT"
# bFLT header: text ends at data_start, image = data..bss_end + stack.
python3 - "$OUT" <<'EOF'
import struct, sys
h = open(sys.argv[1], 'rb').read(40)
_, _, entry, ds, de, be, st, _, rc, fl = struct.unpack('>4s9I', h)
image = be + st - 0x40
print(f"lsh: text {ds - 0x40} data {de - ds} bss {be - de} stack {st} "
      f"-> exec image {image} bytes ({'fits' if image <= 131072 else 'TOO BIG for'} 128 KB)")
EOF
