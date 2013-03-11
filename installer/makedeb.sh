#!/bin/bash

if [ $# -ne 4 ]; then
	echo "usage: $0 path_to_bundle version path_to_udev_rules out_directory"
	exit 1
fi

VERSION="$2"
ARCH=`dpkg-architecture -qDEB_BUILD_ARCH`

if [ "${ARCH}" == "amd64" ]; then
	path_arch=x86_64
	libdir=/usr/lib
else
	path_arch=x86
	libdir=/usr/lib
fi

TOPDIR=/tmp/debbuild-`whoami`-$$

umask 0077
mkdir "${TOPDIR}"
mkdir -p "${TOPDIR}/DEBIAN"
mkdir -p "${TOPDIR}/opt/aktivco/ifd-rutokens/${path_arch}"
cp -r "$1" "${TOPDIR}/opt/aktivco/ifd-rutokens/${path_arch}/"
mkdir -p "${TOPDIR}/${libdir}/pcsc/drivers/ifd-rutokens.bundle/Contents/Linux"
mkdir -p "${TOPDIR}/etc/udev/rules.d/"
RULES_NAME=`basename "$3"`
cp "$3" "${TOPDIR}/opt/aktivco/ifd-rutokens/"

pwd_dir=`pwd`
cd "${TOPDIR}/etc/udev/rules.d/"
ln -sf "/opt/aktivco/ifd-rutokens/"${RULES_NAME} ${RULES_NAME}
cd "${pwd_dir}"
cd "${TOPDIR}/${libdir}/pcsc/drivers/ifd-rutokens.bundle/Contents/Linux"
ln -sf "/opt/aktivco/ifd-rutokens/${path_arch}/ifd-rutokens.bundle/Contents/Linux/librutokens.so."${VERSION} librutokens.so
cd "${pwd_dir}"
cd "${TOPDIR}/${libdir}/pcsc/drivers/ifd-rutokens.bundle/Contents"
ln -sf "/opt/aktivco/ifd-rutokens/${path_arch}/ifd-rutokens.bundle/Contents/Info.plist" Info.plist
cd "${pwd_dir}"
# 'debian/control' need to 'dpkg-shlibdeps'
mkdir -p "${TOPDIR}/debian"
touch "${TOPDIR}/debian/control"
DEPENDS=`cd ${TOPDIR} && dpkg-shlibdeps "opt/aktivco/ifd-rutokens/${path_arch}/ifd-rutokens.bundle/Contents/Linux/librutokens.so."${VERSION} -O 2>/dev/null | sed -e 's/shlibs:Depends=//'`
rm -rf "${TOPDIR}/debian"

cat > "${TOPDIR}/DEBIAN/control" << EOF201301170017
Package: ifd-rutokens
Version: ${VERSION}
Architecture: ${ARCH}
Maintainer: Aktiv Co. <hotline@rutoken.ru>
Installed-Size: `du -skx --exclude="${TOPDIR}/DEBIAN" "${TOPDIR}" | awk '{print $1}'`
Depends: ${DEPENDS}
Section: libs
Priority: optional
Description: Aktiv Co Rutoken S driver
 Allows users to access Rutoken S through pcsc-lite.
EOF201301170017

echo -e '#!/bin/sh\nudevadm control --reload-rules\n' > "${TOPDIR}/DEBIAN/postinst"
echo -e '#!/bin/sh\nudevadm control --reload-rules\n' > "${TOPDIR}/DEBIAN/postrm"

# set permissions
find "${TOPDIR}"/* -type d -exec chmod 755 {} \;
find "${TOPDIR}"/* -type f -exec chmod 644 {} \;
find "${TOPDIR}"/* \( -name *.so.*.*.* -or -name postinst -or -name postrm \) -exec chmod -f 755 {} \;

# make deb packet
fakeroot dpkg-deb --build ${TOPDIR} "$4/ifd-rutokens_${VERSION}_${ARCH}.deb"

rm -rf "${TOPDIR}"

