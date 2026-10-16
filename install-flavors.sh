#!/bin/bash

set -euo pipefail

PATH="/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin:$PATH"
export PATH

INSTALL_MODE="${1:-dkms}"
PROJECT_DIR="$(cd "${2:-$PWD}" && pwd)"
cd "$PROJECT_DIR"

if [ "$EUID" -ne 0 ]; then
	echo "Run this script as root, for example: sudo make install-flavors" >&2
	exit 1
fi

MODULE_NAME="$(tr -d '[:space:]' < MODULE_NAME)"
VERSION="$(tr -d '[:space:]' < VERSION)"

case "$INSTALL_MODE" in
	dkms|static|remove-static) ;;
	*)
		echo "Usage: $0 <dkms|static|remove-static> [project-root]" >&2
		exit 2
		;;
esac

if [ "$INSTALL_MODE" = "dkms" ] && ! command -v dkms >/dev/null 2>&1; then
	echo "dkms is not available" >&2
	exit 1
fi

kernel_versions=()
for build_dir in /lib/modules/*/build; do
	[ -d "$build_dir" ] || continue
	kernel_versions+=("${build_dir%/build}")
done

if [ "${#kernel_versions[@]}" -eq 0 ]; then
	echo "No installed kernel with usable headers was found under /lib/modules" >&2
	exit 1
fi

if [ "$INSTALL_MODE" = "dkms" ] && ! dkms status -m "$MODULE_NAME" -v "$VERSION" 2>/dev/null | grep -q .; then
	dkms add .
fi

flavors="$(printf '%s\n' "${kernel_versions[@]##*/}" | sed 's|.*-||' | sort -u)"
while IFS= read -r flavor; do
	[ -n "$flavor" ] || continue
	latest_kernel=""
	for kernel_version in "${kernel_versions[@]##*/}"; do
		[ "${kernel_version##*-}" = "$flavor" ] || continue
		if [ -z "$latest_kernel" ] || [ "$(printf '%s\n' "$latest_kernel" "$kernel_version" | sort -V | tail -1)" = "$kernel_version" ]; then
			latest_kernel="$kernel_version"
		fi
	done

	[ -n "$latest_kernel" ] || continue
	case "$INSTALL_MODE" in
		dkms)
			echo "Installing $MODULE_NAME/$VERSION through DKMS for kernel $latest_kernel"
			make KVER="$latest_kernel" cleanup-module-files
			dkms build --force -m "$MODULE_NAME" -v "$VERSION" -k "$latest_kernel"
			dkms install --force -m "$MODULE_NAME" -v "$VERSION" -k "$latest_kernel"
			;;
		static)
			echo "Building $MODULE_NAME/$VERSION statically for kernel $latest_kernel"
			make KVER="$latest_kernel" module
			make KVER="$latest_kernel" install-module
			;;
		remove-static)
			echo "Removing static $MODULE_NAME module for kernel $latest_kernel"
			make KVER="$latest_kernel" uninstall-module
			;;
	esac
done <<< "$flavors"

if [ "$INSTALL_MODE" = "static" ]; then
	make install-assets
elif [ "$INSTALL_MODE" = "remove-static" ]; then
	make uninstall-assets
fi
