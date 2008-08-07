#!/bin/sh

KVER=${1}

DIR=${2}

SM="/$DIR/boot/System.map-$KVER"
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
    else
      echo "// Not able to trap do_fork" > config.h
    fi
  fi
  CHROOT=`grep ' sys_chroot' $SM | awk '{print $1}'`
  echo "asmlinkage long (*sys_chroot)(const char __user *filename)=(void *)0x$CHROOT;" >>config.h

  if [ `grep -c sys_call_table $SM` -gt 0 ]; then
    SCT=`grep ' sys_call_table' $SM|awk '{print $1}'`
    echo "#define SCT" >> config.h
    echo "static void **sys_call_table=(void *)0x$SCT;" >> config.h
  fi
  if [ `grep -c ia32_sys_call_table $SM` -gt 0 ]; then
    SCT=`grep ' ia32_sys_call_table' $SM|awk '{print $1}'`
    echo "static void **ia32_sys_call_table=(void *)0x$SCT;" >> config.h
  fi

fi
