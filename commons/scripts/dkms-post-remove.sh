#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
cd "$PROJECT_DIR"

CURRENT_VERSION="$(tr -d '[:space:]' < VERSION)"
MODULE_NAME="$(tr -d '[:space:]' < MODULE_NAME)"

# Keep shared artifacts when another DKMS version of this package is still present.
if command -v dkms >/dev/null 2>&1; then
	OTHER_VERSIONS_COUNT="$(dkms status 2>/dev/null | grep -Ec "^${MODULE_NAME}/" || true)"
	THIS_VERSION_COUNT="$(dkms status 2>/dev/null | grep -Ec "^${MODULE_NAME}/${CURRENT_VERSION}," || true)"

	if [ "$OTHER_VERSIONS_COUNT" -gt "$THIS_VERSION_COUNT" ]; then
		echo "Skipping uninstall-extra: another ${MODULE_NAME} DKMS version is present"
		exit 0
	fi
fi

make uninstall-extra
