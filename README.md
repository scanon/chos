# CHOS

## Introduction

CHOS is a kernel module and utilities that facilitates supporting
multiple Linux releases simultaneously on a single system.  With
chos, you can have a base OS (the true root tree for the system)
and allows users to select from other accessible OS trees.  The
trees must be compatible with the booted kernel.  For example,
you could install Fedora Core 2 as your base operating system
and allows users to transparently use RedHat 7.3, RedHat 8, and
SuSE 9.0.  This allows users to select the distribution that
works for them (or their collaboration).

## Quick Start

1.  Install and configure the following prerequisites

   * A compatible kernel.  For CHOS < 0.13.1, the kernel *must* be
     rebuilt with CONFIG_DEBUG_RODATA=n.  Kernels based on the
     following are supported:
       * EL5 2.6.18 family
       * EL6 2.6.32 family.  This is the most widely tested option.
       * Experimental support for the EL7 3.10.0 family.

   * The associated kernel-devel package.

1.  Download the CHOS code:

        git clone https://github.com/scanon/chos; cd chos

1.  Select a CHOS release to build.  The master branch (and development branches) provide access to the latest features; however, the most recent tagged release is recommended for stability:

        git tag
        git checkout <tag>

1.  Build CHOS with kernel support:

        ./autogen.sh
        ./configure --enable-kernel [ --with-kernel-source=/path/ ]
        make
        make install

1.  Create a suitable /chos/ directory, which will serve as the shared
    chroot directory.  This should include mountpoints for any shared
    filesystems which should be visible inside the CHOS environments.

    A suggested layout is:

        /chos/bin     -> /proc/chos/link/bin
        /chos/boot    -> /proc/chos/link/boot
        /chos/cgroup
        /chos/chos    -> /
        /chos/dev
        /chos/etc     -> /proc/chos/link/etc
        /chos/home
        /chos/lib     -> /proc/chos/link/lib
        /chos/lib32   -> /proc/chos/link/lib32
        /chos/lib64   -> /proc/chos/link/lib64
        /chos/opt     -> /proc/chos/link/opt
        /chos/proc
        /chos/root
        /chos/sbin    -> /proc/chos/link/sbin
        /chos/sys
        /chos/usr     -> /proc/chos/link/usr
        /chos/tmp
        /chos/var     -> /proc/chos/link/var

1. Edit /etc/sysconfig/chos.  Change the value of $BINDMOUNT to be the list of directories in the root environment which should be bind-mounted into their respective paths in /chos/.

1. Edit /etc/chos, the primary CHOS configuration file.  This file should contain a list of environment names and path mappings in the %SHELLS section.  The following example file describes two environments: "root" (the real / environment), and "debian6", located at /export/os/debian6/.

        %SHELLS
        root:/
        debian6:/export/os/debian6

1. Create the CHOS environments using an appropriate tool (e.g., "debootstrap").

1. Install the "chos" utility inside the new CHOS environments.  It is not necessary to build it with kernel support:

        git clone https://github.com/scanon/chos; cd chos
        ./autogen.sh
        ./configure
        make
        make install

1. Install the following symlinks in the CHOS environment:

        etc/chos                -> /local/etc/chos
        etc/chos.conf           -> /local/etc/chos.conf
        etc/group               -> /local/etc/group
        etc/gshadow             -> /local/etc/gshadow
        etc/hosts               -> /local/etc/hosts
        etc/ldap.conf           -> /local/etc/ldap.conf
        etc/localtime           -> /local/etc/localtime
        etc/motd                -> /local/etc/motd
        etc/nsswitch.conf       -> /local/etc/nsswitch.conf
        etc/openldap/ldap.conf  -> /local/etc/openldap/ldap.conf
        etc/passwd              -> /local/etc/passwd
        etc/resolv.conf         -> /local/etc/resolv.conf
        etc/shadow              -> /local/etc/shadow
        etc/ssh/ssh_known_hosts -> /local/etc/ssh/ssh_known_hosts
        var/tmp                 -> /local/var/tmp/
        var/cache               -> /local/var/cache/
        var/db                  -> /local/var/db/
        var/run                 -> /local/var/run/
        var/lock                -> /local/var/lock/
        var/spool               -> /local/var/spool/

1. Start CHOS using the provided initscript:

        service chos start

1. As a non-root user, enter and exit the new CHOS environment:

        env CHOS=debian6 chos
        cat /etc/debian_version
        env CHOS=root chos
        cat /etc/redhat-release
    

## Detailed Installation

See the INSTALL file for more detailed installation instructions.

## Theory

In the end, CHOS is just doing a chroot into the OS tree.  The
kernel module is only needed to simply the framework and allow
the framework to be used for multiple OS trees.  For example,
if you installed a system as follows...

   * / (base OS - OS1)
   * /OS-2 (second OS)
   * /OS-3 (third OS)

You could chroot into /OS-2 and it would function if everything
was installed correctly.  However, for it to fully work you would
need some things to be available in the chroot tree.  For example,
/proc would have to be mounted under each tree (i.e. /OS-2/proc, /OS-3/proc).
Also, if NFS mounts are used, they would need to be mounted under
each tree.  This makes it awkward to support more than a handful
of OS trees.  The kernel module provides a process dependent symbolic
link.  This link will resolve differently for different processes.
Also, children processes automatically inherit the value of their
parent.  Using the kernel modules, you only need this...

   * / (base OS - OS1)
   * /chos
   * /chos/proc
   * /chos/ostrees/OS2
   * /chos/ostrees/OS3

The link, which is accessed through /proc/chos/link, points to the
base of the OS.  The rest of the chos directory contains the subdirectories
that would normally be present in an OS (/bin, /sbin, /usr, etc), but
they are all symlinks that traverse through the special link.  So,
/chos/bin would point to /proc/chos/link/bin, which could translate
to /ostrees/OS2/bin for one user and /ostrees/OS3/bin for another user.

