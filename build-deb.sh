#!/bin/bash
# build-deb.sh - Package a DKMS kernel module repo as an installable .deb
# using debhelper + dh-dkms. Compilation still happens on the target via
# dkms (postinst), matching the existing sudo dkms add/build/install flow.
#
# Usage: build-deb.sh [/path/to/module-repo]
#   MODULE_REPO defaults to PWD.
# Env overrides:
#   DEB_REVISION       (default: 1)
#   OUTPUT_DIR         (default: <module-repo>/dist)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MODULE_REPO="$(cd "${1:-$PWD}" && pwd)"

DEB_REVISION="${DEB_REVISION:-1}"
DEB_MAINTAINER="Sfera Labs <support@sferalabs.cc>"
DEB_HOMEPAGE="${DEB_HOMEPAGE:-https://www.sferalabs.cc/}"
OUTPUT_DIR="${OUTPUT_DIR:-$MODULE_REPO/dist}"

MODULE_NAME="$(tr -d '[:space:]' < "$MODULE_REPO/MODULE_NAME")"
MODULE_VERSION="$(tr -d '[:space:]' < "$MODULE_REPO/VERSION")"
PACKAGE_NAME="${MODULE_NAME}-dkms"
VCS_GIT_URL="$(git -C "$MODULE_REPO" config --get remote.origin.url 2>/dev/null || true)"
COPYRIGHT_NOTICE="$(sed -n 's|.*Copyright (C) ||p' "$MODULE_REPO/module.c" | head -n 1)"

if [ -z "$COPYRIGHT_NOTICE" ]; then
	echo "No copyright notice found in $MODULE_REPO/module.c" >&2
	exit 1
fi

STAGE_DIR="$(mktemp -d)"
trap 'rm -rf "$STAGE_DIR"' EXIT

SRC_DIR="$STAGE_DIR/${PACKAGE_NAME}_${MODULE_VERSION}"
mkdir -p "$SRC_DIR"

# Stage source without development and generated files.
rsync -a \
	--exclude='.*' \
	--exclude='_DEV' \
	--exclude='dist' \
	--exclude='*.md' \
	"$MODULE_REPO"/ "$SRC_DIR"/

mkdir -p "$SRC_DIR/debian"

sed \
	-e "s|@MODULE_NAME@|${MODULE_NAME}|g" \
	-e "s|@PACKAGE_NAME@|${PACKAGE_NAME}|g" \
	-e "s|@DEB_MAINTAINER@|${DEB_MAINTAINER}|g" \
	-e "s|@DEB_HOMEPAGE@|${DEB_HOMEPAGE}|g" \
	-e "s|@VCS_GIT_URL@|${VCS_GIT_URL}|g" \
	"$SCRIPT_DIR/debian/control.in" > "$SRC_DIR/debian/control"


if [ -z "$VCS_GIT_URL" ]; then
	sed '/^Vcs-Git: $/d' "$SRC_DIR/debian/control" > "$SRC_DIR/debian/control.tmp"
	mv "$SRC_DIR/debian/control.tmp" "$SRC_DIR/debian/control"
fi

sed \
	-e "s|@MODULE_NAME@|${MODULE_NAME}|g" \
	-e "s|@DEB_HOMEPAGE@|${DEB_HOMEPAGE}|g" \
	-e "s|@COPYRIGHT_NOTICE@|${COPYRIGHT_NOTICE}|g" \
	"$SCRIPT_DIR/debian/copyright.in" > "$SRC_DIR/debian/copyright"

cp "$SCRIPT_DIR/debian/rules" "$SRC_DIR/debian/rules"
chmod 755 "$SRC_DIR/debian/rules"

sed \
	-e "s|@MODULE_NAME@|${MODULE_NAME}|g" \
	"$SCRIPT_DIR/debian/pkg.dkms.in" > "$SRC_DIR/debian/${PACKAGE_NAME}.dkms"

sed \
	-e "s|@MODULE_NAME@|${MODULE_NAME}|g" \
	"$SCRIPT_DIR/debian/preinst.in" > "$SRC_DIR/debian/${PACKAGE_NAME}.preinst"
chmod 755 "$SRC_DIR/debian/${PACKAGE_NAME}.preinst"

DATE_RFC2822="$(date -R)"
cat > "$SRC_DIR/debian/changelog" <<EOF
${PACKAGE_NAME} (${MODULE_VERSION}-${DEB_REVISION}) unstable; urgency=medium

  * Packaged ${MODULE_NAME} ${MODULE_VERSION}.

 -- ${DEB_MAINTAINER}  ${DATE_RFC2822}
EOF

(
	cd "$SRC_DIR"
	dpkg-buildpackage -us -uc -b
)

mkdir -p "$OUTPUT_DIR"
cp "$STAGE_DIR"/*.deb "$STAGE_DIR"/*.buildinfo "$STAGE_DIR"/*.changes "$OUTPUT_DIR/" 2>/dev/null || true

echo "Built package(s) in $OUTPUT_DIR:"
ls -1 "$OUTPUT_DIR"/*.deb
