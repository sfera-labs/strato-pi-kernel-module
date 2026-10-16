export PATH := /usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin:$(PATH)
SOURCE_DIR ?= $(if $(src),$(src),$(CURDIR))
MODULE_NAME ?= $(strip $(shell cat $(SOURCE_DIR)/MODULE_NAME))
MODULE_VERSION := $(strip $(shell cat $(SOURCE_DIR)/VERSION))
ifeq ($(strip $(MODULE_VERSION_DEFINE)),)
MODULE_VERSION_DEFINE := $(shell echo "$(MODULE_NAME)" | tr '[:lower:]-' '[:upper:]_')_MODULE_VERSION
endif
LEGACY_DTS_NAME := $(strip $(if $(wildcard $(SOURCE_DIR)/DTS_NAME),$(shell cat $(SOURCE_DIR)/DTS_NAME),))

obj-m += $(MODULE_NAME).o
$(MODULE_NAME)-objs := $(MODULE_MAIN_OBJ)
$(foreach m,$(COMMON_MODULES),$(eval $(MODULE_NAME)-objs += commons/$(m)/$(m).o))
$(MODULE_NAME)-objs += $(MODULE_EXTRA_OBJS)

ccflags-y += -D$(MODULE_VERSION_DEFINE)=\"$(MODULE_VERSION)\"

# Use the Kbuild kernel when nested; otherwise default to the running kernel.
BUILD_KVER ?= $(if $(KERNELRELEASE),$(KERNELRELEASE),$(shell uname -r))
# Distinguish an explicit one-kernel request from flavor-wide installation.
KVER_EXPLICIT := $(shell if [ "$(origin KVER)" = undefined ]; then printf no; else printf yes; fi)
# Resolve the kernel version used for module paths and install commands.
TARGET_KVER := $(if $(KVER),$(KVER),$(BUILD_KVER))
# Cache command availability.
DKMS_AVAILABLE := $(shell if command -v dkms >/dev/null 2>&1 || [ -x /usr/sbin/dkms ]; then printf yes; else printf no; fi)
DPKG_QUERY_AVAILABLE := $(shell if command -v dpkg-query >/dev/null 2>&1; then printf yes; else printf no; fi)

INSTALL_MODE ?= dkms
DKMS_DEB_PACKAGE := $(MODULE_NAME)-dkms
KDIR ?= /lib/modules/$(TARGET_KVER)/build
OVERLAY_DIR ?= $(shell [ -d /boot/overlays ] && echo /boot/overlays || echo /boot/firmware/overlays)
DTBO_DIR ?= $(SOURCE_DIR)

.PHONY: all module dtbo clean
.PHONY: install install-dkms install-dkms-kernel uninstall-dkms uninstall-dkms-kernel
.PHONY: install-static install-static-kernel uninstall-static uninstall-static-kernel
.PHONY: install-module uninstall-module cleanup-module-files check-dkms-unmanaged
.PHONY: install-flavors install-assets uninstall-assets deb

# Build path: default make and DKMS builds use this target.
all: dtbo module

# Build path: compile the module for TARGET_KVER under make or DKMS.
module:
	make -C $(KDIR) M=$(PWD) modules

# Build path: compile board overlays into DTBO_DIR (default make, DKMS, install-assets).
dtbo:
	@set -e; \
	for dts in "$(SOURCE_DIR)/$(MODULE_NAME)"*.dts; do \
		[ -f "$$dts" ] || continue; \
		name="$${dts##*/}"; \
		name="$${name%.dts}"; \
		dtc -@ -Hepapr -I dts -O dtb -o "$(DTBO_DIR)/$$name.dtbo" "$$dts"; \
	done

# Build cleanup: used by make clean and the DKMS clean hook.
clean:
	make -C $(KDIR) M=$(PWD) clean
	rm -f $(MODULE_NAME)*.dtbo

# Make install path: use DKMS when available, otherwise install statically.
install:
	@if [ "$(DKMS_AVAILABLE)" = yes ]; then \
		$(MAKE) install-dkms; \
	else \
		echo "WARNING: DKMS is unavailable; installing statically. This will not rebuild automatically after kernel updates."; \
		$(MAKE) install-static; \
	fi

# Make DKMS install path: explicit KVER or latest kernel per flavor.
install-dkms:
	@if [ "$(DKMS_AVAILABLE)" = no ]; then \
		echo "DKMS is required for install-dkms" >&2; \
		exit 1; \
	fi
	@if [ "$(KVER_EXPLICIT)" = "yes" ]; then \
		$(MAKE) install-dkms-kernel KVER="$(KVER)"; \
	else \
		$(MAKE) install-flavors INSTALL_MODE=dkms; \
	fi

# Make static install path: explicit KVER or latest kernel per flavor.
install-static:
	@if [ "$(KVER_EXPLICIT)" = "yes" ]; then \
		$(MAKE) install-static-kernel KVER="$(KVER)"; \
	else \
		$(MAKE) install-flavors INSTALL_MODE=static; \
	fi

# Make DKMS install path: register if needed, then install one KVER.
install-dkms-kernel:
	@if [ -z "$(KVER)" ]; then \
		echo "KVER is required: sudo make install-dkms KVER=<kernel-version>" >&2; \
		exit 1; \
	fi
	@if ! sudo dkms status -m "$(MODULE_NAME)" -v "$(MODULE_VERSION)" 2>/dev/null | grep -q .; then \
		sudo dkms add "$(SOURCE_DIR)"; \
	fi
	$(MAKE) KVER="$(TARGET_KVER)" cleanup-module-files
	sudo dkms install --force -m "$(MODULE_NAME)" -v "$(MODULE_VERSION)" -k "$(TARGET_KVER)"

# Make DKMS uninstall path; apt removal runs dkms remove from the dh_dkms-generated prerm.
uninstall-dkms:
	$(MAKE) check-dkms-unmanaged
	@if [ -z "$(DKMS_AVAILABLE)" ]; then \
		echo "DKMS is required for uninstall-dkms" >&2; \
		exit 1; \
	fi
	@if [ "$(KVER_EXPLICIT)" = "yes" ]; then \
		$(MAKE) uninstall-dkms-kernel KVER="$(KVER)"; \
	else \
		if sudo dkms status -m "$(MODULE_NAME)" -v "$(MODULE_VERSION)" 2>/dev/null | grep -q .; then \
			sudo dkms remove -m "$(MODULE_NAME)" -v "$(MODULE_VERSION)" --all; \
		else \
			echo "No DKMS registration found for $(MODULE_NAME)/$(MODULE_VERSION)"; \
		fi; \
	fi

# Make DKMS uninstall path for one KVER; not used by apt removal.
uninstall-dkms-kernel:
	$(MAKE) check-dkms-unmanaged
	@if [ -z "$(KVER)" ]; then \
		echo "KVER is required: make uninstall-dkms KVER=<kernel-version>" >&2; \
		exit 1; \
	fi
	@if ! sudo dkms status -m "$(MODULE_NAME)" -v "$(MODULE_VERSION)" -k "$(KVER)" 2>/dev/null | grep -q ': installed'; then \
		echo "No installed DKMS module for $(MODULE_NAME)/$(MODULE_VERSION) on $(KVER)"; \
	else \
		sudo dkms uninstall -m "$(MODULE_NAME)" -v "$(MODULE_VERSION)" -k "$(KVER)"; \
	fi

# Guard for Make DKMS uninstall paths; apt manages packaged installations.
check-dkms-unmanaged:
	@if [ "$(DPKG_QUERY_AVAILABLE)" = yes ] && dpkg-query -W -f='$${db:Status-Status}' "$(DKMS_DEB_PACKAGE)" 2>/dev/null | grep -qx installed; then \
		echo "$(DKMS_DEB_PACKAGE) is dpkg-managed; remove it with apt instead" >&2; \
		exit 1; \
	fi

# Make static install path: build and install one KVER plus shared assets.
install-static-kernel:
	@if [ -z "$(KVER)" ]; then \
		echo "KVER is required: make install-static KVER=<kernel-version>" >&2; \
		exit 1; \
	fi
	$(MAKE) KVER="$(KVER)" module
	$(MAKE) KVER="$(KVER)" install-module
	sudo $(MAKE) install-assets

# Make static install path: copy a built module into one kernel tree.
install-module:
	@if [ -z "$(KVER)" ]; then \
		echo "KVER is required: make install-module KVER=<kernel-version>" >&2; \
		exit 1; \
	fi
	$(MAKE) KVER="$(TARGET_KVER)" cleanup-module-files
	sudo install -D -m 644 -c $(MODULE_NAME).ko /lib/modules/$(TARGET_KVER)/updates/dkms/$(MODULE_NAME).ko
	sudo depmod "$(TARGET_KVER)"

# Make install/uninstall paths: clear this module's non-dpkg-managed KVER files.
cleanup-module-files:
	@if [ -z "$(KVER)" ]; then \
		echo "KVER is required: make cleanup-module-files KVER=<kernel-version>" >&2; \
		exit 1; \
	fi
	@set -e; \
	module_root="/lib/modules/$(TARGET_KVER)"; \
	module_files="$$(find "$$module_root" \( -type f -o -type l \) -name "$(MODULE_NAME).ko*")"; \
	for module_file in $$module_files; do \
		if [ "$(DPKG_QUERY_AVAILABLE)" = yes ]; then \
			module_owner="$$(dpkg-query -S "$$module_file" 2>/dev/null || true)"; \
			if [ -n "$$module_owner" ]; then \
				module_package="$${module_owner%: *}"; \
				echo "Package $$module_package owns $$module_file; remove it first with 'sudo apt remove $$module_package'" >&2; \
				exit 1; \
			fi; \
		fi; \
	done; \
	for module_file in $$module_files; do \
		sudo rm -f "$$module_file"; \
	done; \
	if [ -n "$$module_files" ]; then sudo depmod "$(TARGET_KVER)"; fi

# Make uninstall path: remove module files for one KVER and refresh depmod.
uninstall-module:
	@if [ -z "$(KVER)" ]; then \
		echo "KVER is required: make uninstall-module KVER=<kernel-version>" >&2; \
		exit 1; \
	fi
	$(MAKE) KVER="$(TARGET_KVER)" cleanup-module-files
	sudo depmod "$(TARGET_KVER)"

# Make static uninstall path: explicit KVER or latest kernel per flavor.
uninstall-static:
	@if [ "$(KVER_EXPLICIT)" = "yes" ]; then \
		$(MAKE) uninstall-static-kernel KVER="$(KVER)"; \
	else \
		$(MAKE) install-flavors INSTALL_MODE=remove-static; \
	fi

# Make static uninstall path: remove one KVER and shared assets.
uninstall-static-kernel:
	@if [ -z "$(KVER)" ]; then \
		echo "KVER is required: make uninstall-static KVER=<kernel-version>" >&2; \
		exit 1; \
	fi
	$(MAKE) KVER="$(KVER)" uninstall-module
	sudo $(MAKE) uninstall-assets

# DKMS post-install hook (also run by .deb postinst) and Make static path: install overlays/rules.
# Overlays are built in a temp dir so stale .dtbo files in SOURCE_DIR are never installed.
install-assets:
	@set -e; \
	if [ -n "$(UDEV_GROUP)" ] && ! getent group "$(UDEV_GROUP)" >/dev/null 2>&1; then \
		groupadd --system "$(UDEV_GROUP)"; \
	fi; \
	dtbo_dir="$$(mktemp -d)"; \
	trap 'rm -rf "$$dtbo_dir"' EXIT; \
	$(MAKE) dtbo DTBO_DIR="$$dtbo_dir"; \
	for dtbo in "$$dtbo_dir"/*.dtbo; do \
		[ -f "$$dtbo" ] || continue; \
		install -D -m 644 -c "$$dtbo" "$(OVERLAY_DIR)/$${dtbo##*/}"; \
	done; \
	if [ -n "$(LEGACY_DTS_NAME)" ] && [ -f "$$dtbo_dir/$(LEGACY_DTS_NAME).dtbo" ]; then \
		install -D -m 644 -c "$$dtbo_dir/$(LEGACY_DTS_NAME).dtbo" "$(OVERLAY_DIR)/$(MODULE_NAME).dtbo"; \
	fi; \
	for rule in $(UDEV_RULES); do \
		install -D -m 644 -c "$$rule" "/etc/udev/rules.d/$$rule"; \
	done; \
	udevadm control --reload-rules || true; \
	udevadm trigger || true

# DKMS post-remove hook (run by dkms remove, also from the .deb prerm) and Make static path.
uninstall-assets:
	rm -f $(OVERLAY_DIR)/$(MODULE_NAME)*.dtbo
	@if [ -n "$(strip $(UDEV_RULES))" ]; then \
		for rule in $(UDEV_RULES); do \
			rm -f /etc/udev/rules.d/$$rule; \
		done; \
	fi
	udevadm control --reload-rules || true
	udevadm trigger || true

# Package build path: make deb creates the local DKMS .deb artifact.
deb:
	bash $(SOURCE_DIR)/commons/scripts/build-deb.sh $(SOURCE_DIR)

# Make flavor-wide install/uninstall paths, using kernels with installed headers.
install-flavors:
	sudo bash $(SOURCE_DIR)/commons/scripts/install-flavors.sh "$(INSTALL_MODE)" $(SOURCE_DIR)
