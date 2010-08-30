#!/bin/sh

KVER=${1}

DIR=${2}

SM="/$DIR/boot/System.map-$KVER"
KB="/boot/vmlinux-$KVER"
[ -e /usr/lib/debug/lib/modules/$KVER/vmlinux  ] && KB="/usr/lib/debug/lib/modules/$KVER/vmlinux"
INC="address.h"

if [ -e "$INC" ] ; then
  rm "$INC"
fi
touch "$INC"
if  [ -e $SM ] ; then 
  address=`grep do_fork $SM|grep -v idle|sed 's/ .*//'`
  if [ -e $KB ] ; then
    end=`objdump -d $KB|grep -A5 "^$address"|tail -1|sed 's/:.*//'`
    echo "#define START_ADD  0x$address" > $INC
      echo "#define END_ADD    0x$end" >> $INC
  else
    if [ -e $KB.gz ] ; then
      zcat $KB.gz > ./vmlinux
      end=`objdump -d ./vmlinux|grep -A5 "^$address"|tail -1|sed 's/:.*//'`
      rm ./vmlinux
      echo "#define START_ADD  0x$address" > $INC
      echo "#define END_ADD    0x$end" >> $INC
    else
      echo "#define START_ADD  0x$address" > $INC
      if [ $(uname -m|grep -c x86_64) -gt 0 ] ; then
        echo "#define LENGTH    0xC" >> $INC
        echo 'unsigned char opcode[] =   "\x41\x57\x49\x89\xd7\x41\x56\x49\x89\xce\x41\x55";' >> $INC
      else
        echo "#define LENGTH    0x6" >> $INC
        echo 'unsigned char opcode[] =   "\x55\x89\xcd\x57\x89\xc7\x56";' >> $INC
      fi
    fi
  fi
  CHROOT=`grep ' sys_chroot' $SM | awk '{print $1}'`
  echo "asmlinkage long (*sys_chroot)(const char __user *filename)=(void *)0x$CHROOT;" >>$INC

fi
