#!/bin/bash
# buildInstaller.sh Makes installer of driver for Mac
# Use: buildInstaller.sh dest_path bundle_path
#    Copyright (C) Aktiv Co  <hotline@rutoken.ru>

#!/bin/bash
echo "Mac installer is running..."

if [ $# -ne 2 ]; then
	echo "usage: $0 path_to_bundle out_directory"
	exit 1
fi

DST_PATH=$2
BUNDLEPATH=$1
SHPATH=`echo $0 | sed 's/\/[^\/]*$//'`
TMPDIR=/tmp/pkgbuild-`whoami`-$$

mkdir -p $TMPDIR/installer/src
mkdir $TMPDIR/installer/out
mkdir $TMPDIR/installer/pkg
cp -r $BUNDLEPATH $TMPDIR/installer/src/
pkgbuild --info $SHPATH/installer-cfg/PackageInfo --scripts $SHPATH/installer-cfg/scripts --root $TMPDIR/installer/src $TMPDIR/installer/pkg/ifd-rutokens.bundle
productbuild --distribution $SHPATH/installer-cfg/Distribution --resources "$SHPATH/installer-cfg/Resources" --package-path $TMPDIR/installer/pkg/ $TMPDIR/installer/out/ifd-rutokens.pkg
mv $TMPDIR/installer/out/ifd-rutokens.pkg $DST_PATH
rm -rf $TMPDIR
