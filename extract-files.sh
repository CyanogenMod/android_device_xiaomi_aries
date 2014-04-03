#!/bin/sh

VENDOR=xiaomi
DEVICE=aries

if [ $# -eq 1 ]; then
    COPY_FROM=$1
    test ! -d "$COPY_FROM" && echo error reading dir "$COPY_FROM" && exit 1
fi

BASE=../../../vendor/$VENDOR/$DEVICE/proprietary
rm -rf $BASE/*

for FILE in `cat proprietary-blobs.txt | grep -v ^# | grep -v ^$`; do
    DIR=`dirname $FILE`
    if [ ! -d $BASE/$DIR ]; then
        mkdir -p $BASE/$DIR
    fi
    if [ "$COPY_FROM" = "" ]; then
      adb pull /system/$FILE $BASE/$FILE
    else
      cp $COPY_FROM/$FILE $BASE/$FILE
    fi
done

./setup-makefiles.sh
