#!/bin/bash
# buildInstaller.sh Makes installer of driver & udev rules
# Use: buildInstaller.sh template tar rules output
#    Copyright (C) Aktiv Co  <hotline@rutoken.ru>

if [ $# -ne 4 ]; then
	echo "usage: $0 path_to_template path_to_tar path_to_udev_rules output_path"
	exit 1
fi

TEMPLATE=$1
TAR=$2
RULES=$3
OUTPUT=$4

filename=`echo $RULES | sed 's/.*\///'`
cat $TEMPLATE | sed "/^__RULES_BELOW__/r ${RULES}" | sed "/^__RULES_BELOW__/ a $filename" >$OUTPUT
cat $TAR >> $OUTPUT
chmod +x $OUTPUT

exit 0
