LCD_SHELL_VERSION = 1
LCD_SHELL_SITE = $(BR2_EXTERNAL_STM32F769_PATH)/package/lcd-shell/src
LCD_SHELL_SITE_METHOD = local

define LCD_SHELL_BUILD_CMDS
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_LDFLAGS) -o $(@D)/lcd-shell $(@D)/lcd-shell.c
endef

define LCD_SHELL_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/lcd-shell $(TARGET_DIR)/usr/bin/lcd-shell
endef

$(eval $(generic-package))
