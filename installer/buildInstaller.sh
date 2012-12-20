#!/bin/bash
# buildInstaller.sh Makes installer of driver & udev rules
# Use: buildInstaller.sh template tar rules output
#    Copyright (C) Aktiv Co  <hotline@rutoken.ru>

if [ -z $1 ] ; then
	echo "Template not specified. Exit."
	exit 1
fi
if [ -z $2 ] ; then
	echo "Tar not specified. Exit."
	exit 1
fi
if [ -z $3 ] ; then
	echo "Rules not specified. Exit."
	exit 1
fi
if [ -z $4 ] ; then
	echo "Output file not specified. Exit."
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
