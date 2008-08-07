#!/bin/sh

KVER=${1}

SM="/boot/System.map-$KVER"
KB="/boot/vmlinux-$KVER"

if [ -e "config.h" ] ; then
  rm "config.h"
fi
touch "config.h"
if  [ -e $SM ] ; then 
  address=`grep do_fork $SM|sed 's/ .*//'`
  if [ -e $KB ] ; then
    end=`objdump -d $KB|grep -A5 "^$address"|tail -1|sed 's/:.*//'`
    echo "#define START_ADD  0x$address" > config.h
    echo "#define END_ADD    0x$end" >> config.h
  else
    if [ -e $KB.gz ] ; then
      zcat $KB.gz > ./vmlinux
      end=`objdump -d ./vmlinux|grep -A5 "^$address"|tail -1|sed 's/:.*//'`
      rm ./vmlinux
      echo "#define START_ADD  0x$address" > config.h
      echo "#define END_ADD    0x$end" >> config.h
    fi
  fi
fi

