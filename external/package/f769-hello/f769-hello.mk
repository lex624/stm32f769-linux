################################################################################
#
# f769-hello
#
################################################################################

F769_HELLO_VERSION = 1.0
F769_HELLO_SITE = $(BR2_EXTERNAL_STM32F769_PATH)/package/f769-hello/src
F769_HELLO_SITE_METHOD = local
F769_HELLO_LICENSE = MIT

define F769_HELLO_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) $(TARGET_CONFIGURE_OPTS) -C $(@D)
endef

define F769_HELLO_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/f769-hello $(TARGET_DIR)/usr/bin/f769-hello
endef

$(eval $(generic-package))
