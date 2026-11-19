F769_BENCH_VERSION = 2026.94
F769_BENCH_SITE = https://matt.ucc.asn.au/dropbear/releases
F769_BENCH_SOURCE = dropbear-$(F769_BENCH_VERSION).tar.bz2
F769_BENCH_LICENSE = MIT, BSD-2-Clause, Public domain, Unlicense, WTFPL
F769_BENCH_LICENSE_FILES = LICENSE libtomcrypt/LICENSE libtommath/LICENSE
F769_BENCH_REPO = $(BR2_EXTERNAL_STM32F769_PATH)/..
F769_BENCH_FLAGS = -Os -mthumb -mcpu=cortex-m7

define F769_BENCH_CONFIGURE_CMDS
	cp $(F769_BENCH_REPO)/bench/dropbear-options.h $(@D)/localoptions.h
	cd $(@D) && $(TARGET_CONFIGURE_OPTS) ac_cv_func_fork=no \
		CFLAGS="$(F769_BENCH_FLAGS) -ffunction-sections -fdata-sections" \
		LDFLAGS="-Wl,--gc-sections -Wl,-elf2flt=-s65536" ./configure \
		--host=$(GNU_TARGET_NAME) --disable-harden --disable-zlib \
		--disable-lastlog --disable-utmp --disable-utmpx --disable-wtmp \
		--disable-wtmpx --enable-bundled-libtom
endef
define F769_BENCH_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) PROGRAMS="dropbear dropbearkey"
	$(TARGET_CC) $(F769_BENCH_FLAGS) -Wl,-elf2flt -o $(@D)/bluenrg-probe $(F769_BENCH_REPO)/bench/bluenrg-probe.c
	$(TARGET_CC) $(F769_BENCH_FLAGS) -Wl,-elf2flt -o $(@D)/ssh-listener $(F769_BENCH_REPO)/bench/ssh-listener.c
	$(TARGET_CC) $(F769_BENCH_FLAGS) -Wl,-elf2flt -o $(@D)/f769-control $(F769_BENCH_REPO)/port/f769-control.c
	$(TARGET_CC) $(F769_BENCH_FLAGS) -Wl,-elf2flt -o $(@D)/frame-sink $(F769_BENCH_REPO)/bench/frame-sink.c
	$(TARGET_CC) $(F769_BENCH_FLAGS) -Wl,-elf2flt -o $(@D)/usb-tether $(F769_BENCH_REPO)/bench/usb-tether.c
endef
define F769_BENCH_INSTALL_TARGET_CMDS
	$(INSTALL) -m 0755 $(@D)/dropbear $(@D)/ssh-listener $(TARGET_DIR)/usr/sbin/
	$(INSTALL) -m 0755 $(@D)/dropbearkey $(@D)/bluenrg-probe $(@D)/f769-control $(@D)/frame-sink $(@D)/usb-tether $(TARGET_DIR)/usr/bin/
endef
$(eval $(generic-package))
