SOURCE_DIR ?= $(if $(src),$(src),$(CURDIR))
MODULE_NAME ?= $(strip $(shell cat $(SOURCE_DIR)/MODULE_NAME))
MODULE_VERSION := $(strip $(shell cat $(SOURCE_DIR)/VERSION))
ifeq ($(strip $(MODULE_VERSION_DEFINE)),)
MODULE_VERSION_DEFINE := $(shell echo "$(MODULE_NAME)" | tr '[:lower:]-' '[:upper:]_')_MODULE_VERSION
endif
DTS_NAME ?= $(strip $(shell cat $(SOURCE_DIR)/DTS_NAME))
ifeq ($(strip $(DTS_NAME)),)
DTS_NAME := $(MODULE_NAME)
endif

obj-m += $(MODULE_NAME).o
$(MODULE_NAME)-objs := $(MODULE_MAIN_OBJ)
$(foreach m,$(COMMON_MODULES),$(eval $(MODULE_NAME)-objs += commons/$(m)/$(m).o))
$(MODULE_NAME)-objs += $(MODULE_EXTRA_OBJS)

ccflags-y += -D$(MODULE_VERSION_DEFINE)=\"$(MODULE_VERSION)\"

KVER ?= $(if $(KERNELRELEASE),$(KERNELRELEASE),$(shell uname -r))
KDIR ?= /lib/modules/$(KVER)/build

all: dtbo
	make -C $(KDIR) M=$(PWD) modules

dtbo: $(DTS_NAME).dts
	dtc -@ -Hepapr -I dts -O dtb -o $(MODULE_NAME).dtbo $(DTS_NAME).dts

clean:
	make -C $(KDIR) M=$(PWD) clean
	rm -f $(MODULE_NAME).dtbo

install:
	sudo install -D -m 644 -c $(MODULE_NAME).ko /lib/modules/$(KVER)/updates/dkms/$(MODULE_NAME).ko
	sudo depmod
	sudo $(MAKE) install-extra

install-extra: dtbo
	install -D -m 644 -c $(MODULE_NAME).dtbo /boot/overlays/$(MODULE_NAME).dtbo
	@if [ -n "$(strip $(UDEV_RULES))" ]; then \
		for rule in $(UDEV_RULES); do \
			install -D -m 644 -c $$rule /etc/udev/rules.d/$$rule; \
		done; \
	fi
	udevadm control --reload-rules || true
	udevadm trigger || true

uninstall-extra:
	rm -f /boot/overlays/$(MODULE_NAME).dtbo
	@if [ -n "$(strip $(UDEV_RULES))" ]; then \
		for rule in $(UDEV_RULES); do \
			rm -f /etc/udev/rules.d/$$rule; \
		done; \
	fi
	udevadm control --reload-rules || true
	udevadm trigger || true
