#!/bin/bash
# buildInstaller.sh Makes installer of driver for Mac
# Use: buildInstaller.sh dest_path bundle_path
#    Copyright (C) Aktiv Co  <hotline@rutoken.ru>

echo "Mac installer is running..."

if [ -z $1 ] ; then
	echo "Destination path not specified. Exit."
	exit 1
fi
if [ -z $2 ] ; then
	echo "Bundle path not specified. Exit."
	exit 1
fi
DST_PATH=$1
BUNDLEPATH=$2
SHPATH=`echo $0 | sed 's/\/[^\/]*$//'`

mkdir -p $DST_PATH/installer/src
mkdir $DST_PATH/installer/out
mkdir $DST_PATH/installer/pkg
cp -r $BUNDLEPATH $DST_PATH/installer/src/
pkgbuild --info $SHPATH/installer-cfg/PackageInfo --root $DST_PATH/installer/src $DST_PATH/installer/pkg/ifd-rutokens.bundle
productbuild --distribution $SHPATH/installer-cfg/Distribution --package-path $DST_PATH/installer/pkg/ $DST_PATH/installer/out/ifd-rutokens.pkg
cp $DST_PATH/installer/out/ifd-rutokens.pkg $DST_PATH
rm -rf $DST_PATH/installer
