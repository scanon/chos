#!/bin/sh

set -Eeu

KVER=${1}

if [ $# -eq 2 ]; then
  DIR=${2}
else
  DIR=""
fi

SM="/$DIR/boot/System.map-$KVER"
KB="/boot/vmlinux-$KVER"
[ -e /usr/lib/debug/lib/modules/$KVER/vmlinux  ] && KB="/usr/lib/debug/lib/modules/$KVER/vmlinux"
INC="address.h"

if [ -e "$INC" ] ; then
  rm "$INC"
fi
touch "$INC"
if  [ -e $SM ] ; then 
  address=`grep do_fork$ $SM|grep -v idle|sed 's/ .*//'`
  if [ -e $KB ] ; then

    # ffffffff8106a760:       55                      push   %rbp
    # ffffffff8106a761:       48 89 e5                mov    %rsp,%rbp
    # ffffffff8106a764:       48 81 ec f0 00 00 00    sub    $0xf0,%rsp
    # ffffffff8106a76b:       48 89 5d d8             mov    %rbx,-0x28(%rbp)
    # ffffffff8106a76f:       4c 89 65 e0             mov    %r12,-0x20(%rbp)
    # ffffffff8106a773:       4c 89 6d e8             mov    %r13,-0x18(%rbp)
    # ffffffff8106a777:       4c 89 75 f0             mov    %r14,-0x10(%rbp)
    # ffffffff8106a77b:       4c 89 7d f8             mov    %r15,-0x8(%rbp)
    # ffffffff8106a77f:       e8 7c 06 fa ff          callq  0xffffffff8100ae00
    # ffffffff8106a784:       65 48 8b 04 25 28 00    mov    %gs:0x28,%rax
    # ffffffff8106a78b:       00 00
    # ffffffff8106a78d:       48 89 45 c8             mov    %rax,-0x38(%rbp)
    # ffffffff8106a791:       31 c0                   xor    %eax,%eax
    end=`objdump -d $KB|grep -A8 "^$address"|tail -1|sed 's/:.*//'`
    echo "#define START_ADD  0x$address" > $INC
      echo "#define END_ADD    0x$end" >> $INC
  else
    if [ -e $KB.gz ] ; then
      zcat $KB.gz > ./vmlinux
      end=`objdump -d ./vmlinux|grep -A8 "^$address"|tail -1|sed 's/:.*//'`
      rm ./vmlinux
      echo "#define START_ADD  0x$address" > $INC
      echo "#define END_ADD    0x$end" >> $INC
    else
      echo "#define START_ADD  0x$address" > $INC
      echo '#ifdef RHEL_MAJOR' >> $INC
      if [ $(uname -m|grep -c x86_64) -gt 0 ] ; then
        echo '#if LINUX_VERSION_CODE == KERNEL_VERSION(2,6,18) && RHEL_MAJOR == 5' >> $INC
        echo "#define LENGTH    0xC" >> $INC
        echo 'unsigned char opcode[] =   "\x41\x57\x49\x89\xd7\x41\x56\x49\x89\xce\x41\x55";' >> $INC
        echo '#elif LINUX_VERSION_CODE == KERNEL_VERSION(2,6,32) && RHEL_MAJOR == 6' >> $INC
        echo "#define LENGTH    0x1f" >> $INC
        echo 'unsigned char opcode[] =   "\x55\x48\x89\xe5\x48\x81\xec\xb0\x00\x00\x00\x48\x89\x5d\xd8\x4c\x89\x65\xe0\x4c\x89\x6d\xe8\x4c\x89\x75\xf0\x4c\x89\x7d\xf8";' >> $INC
        echo '#elif LINUX_VERSION_CODE == KERNEL_VERSION(3,10,0) && RHEL_MAJOR == 7' >> $INC
        echo "#define LENGTH    0x19" >> $INC
        echo 'unsigned char opcode[] =   "\x0f\x1f\x44\x00\x00\x55\x48\x89\xe5\x41\x57\x41\x56\x41\x55\x49\x89\xfd\x41\x54\x53\x48\x83\xec\x28";' >> $INC
        echo '#endif' >> $INC
      else
        echo '#if LINUX_VERSION_CODE == KERNEL_VERSION(2,6,18) && RHEL_MAJOR == 5' >> $INC
        echo "#define LENGTH    0x6" >> $INC
        echo 'unsigned char opcode[] =   "\x55\x89\xcd\x57\x89\xc7\x56";' >> $INC
        echo '#endif' >> $INC
      fi
      if [ $(uname -m|grep -c x86_64) -gt 0 ] ; then
        echo '#else' >> $INC
        echo '#if LINUX_VERSION_CODE == KERNEL_VERSION(2,6,32)' >> $INC
        echo "#define LENGTH    0x3a" >> $INC
        echo 'unsigned char opcode[] =   "\x41\x57\x41\x56\x49\x89\xce\x41\x55\x49\x89\xfd\x41\x54\x55\x4c\x89\xcd\x53\x48\x89\xd3\x48\x83\xec\x78\x65\x48\x8b\x04\x25\x28\x00\x00\x00\x48\x89\x44\x24\x68\x31\xc0\xf7\xc7\x00\x00\x00\x10\x48\x89\x74\x24\x20\x4c\x89\x44\x24\x18";' >> $INC
        echo '#endif' >> $INC
      fi
      echo '#endif /* RHEL_MAJOR */' >> $INC
    fi
  fi
  CHROOT=`grep ' sys_chroot' $SM | awk '{print $1}'`
  echo "asmlinkage long (*sys_chroot)(const char __user *filename)=(void *)0x$CHROOT;" >>$INC
  
  # find_task_by_pid_ns and tasklist_lock are not exported
  FIND_PID=`grep ' find_task_by_pid_ns$' $SM | awk '{print $1}'`
  LOOKUP_ADDRESS=`grep ' lookup_address$' $SM | awk '{print $1}'`
  TASKLIST_LOCK=`grep ' tasklist_lock$' $SM | awk '{print $1}'`
  SET_FS_ROOT=`grep ' set_fs_root$' $SM | awk '{print $1}'`
  PATH_LOOKUPAT=`grep ' path_lookupat$' $SM | awk '{print $1}'`
  echo "#ifdef PID_NS" >>$INC;
  echo "struct task_struct* (*s_find_task_by_pid_ns)(pid_t nr, struct pid_namespace *ns)=(void *)0x$FIND_PID;" >>$INC;
  echo "#endif" >>$INC;
  echo "rwlock_t *tasklist_lock_p = (rwlock_t *)0x$TASKLIST_LOCK;" >>$INC;
  echo "#ifdef STRUCT_PATH" >>$INC;
  echo "void* (*set_fs_root_p)(struct fs_struct *, struct path *)=(void *)0x$SET_FS_ROOT;" >>$INC;
  echo "#else" >>$INC;
  echo "void* (*set_fs_root_p)(struct fs_struct *, struct vfsmount *, struct dentry *)=(void *)0x$SET_FS_ROOT;" >>$INC;
  echo "#endif" >>$INC;
  echo "#ifndef HAS_PATH_LOOKUP" >>$INC;
  echo "int (*path_lookupat_p)(int, const char *, unsigned int, struct nameidata *)=(void *)0x$PATH_LOOKUPAT;" >>$INC;
  echo "#endif" >>$INC;
  echo "pte_t* (*lookup_address_p)(unsigned long, unsigned int *)=(void *)0x$LOOKUP_ADDRESS;" >>$INC;


fi
