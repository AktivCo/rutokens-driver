#!/bin/bash

if [ $# -ne 5 ]; then
	echo "usage: $0 target_platform path_to_bundle version path_to_udev_rules out_directory"
	exit 1
fi

TOPDIR=/tmp/rpmbuild-`whoami`-$$
ABSOLUTE_PATH_TO_SCRIPT=`readlink -e "$0"`
SCRIPT_DIR=`dirname "${ABSOLUTE_PATH_TO_SCRIPT}"`

umask 0077 && mkdir -p "${TOPDIR}/SOURCES" && mkdir -p "${TOPDIR}/BUILD" && mkdir -p "${TOPDIR}/RPMS"
cp -r "$2" "${TOPDIR}/SOURCES"
cp "$4" "${TOPDIR}/SOURCES/95-rutokens.rules"


rpmbuild -vv -bb --define "_topdir ${TOPDIR}" --define "version $3" --target "$1" "${SCRIPT_DIR}/ifd-rutokens.spec"

find "${TOPDIR}/RPMS" -name '*.rpm' -exec cp {} "$5" \;

rm -rf "${TOPDIR}"

