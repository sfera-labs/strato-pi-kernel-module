MODULE_MAIN_OBJ := module.o
COMMON_MODULES := utils gpio atecc
MODULE_EXTRA_OBJS := commons/soft_uart/raspberry_soft_uart.o commons/soft_uart/queue.o commons/commons.o
UDEV_RULES := 99-stratopi.rules

SOURCE_DIR := $(if $(src),$(src),$(CURDIR))
include $(SOURCE_DIR)/commons/scripts/kmod-common.mk
