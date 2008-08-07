# This is a recursive approach.
# When make is run in the module directory, KERNELRELEASE will
# not be defined.  As a result, the kernel make stucture will
# be called specifying that the module directory should be built.
# This will run make again in the module directory, but KERNELRELEASE
# will be defined.
#
#

MOD=chos.o
MODMODE=644

DKMS=$(FAKEROOT)/usr/src/chos-$(VERSION)

include ../Make.Rules
ifneq ($(KERNELRELEASE),)

obj-m	+= $(MOD)
chos-objs := chos_lkm.o

# If this is a 2.4 kernel...
ifeq ($(PATCHLEVEL),4)
include $(TOPDIR)/Rules.make

chos.o: chos_lkm.o
chos.o: $(chos-objs)
	$(LD) -r -o $@ $(chos-objs)
endif
#modules: chos_lkm.o
else
KDIR	:= $(KERNEL_BASE)
PWD		:= $(shell pwd)

all:
ifeq ($(BUILD_KERNEL),1)
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) modules
endif
endif

install:
ifeq ($(BUILD_KERNEL),1)
	$(MKDIR) $(FAKEROOT)$(MODULES_DIR)
	$(INSTALL) -m $(MODMODE) $(MOD) $(FAKEROOT)$(MODULES_DIR)
endif

installdkms:
	$(MKDIR) $(DKMS)
	$(INSTALL) -m 644 chos_lkm.c $(DKMS)
	$(INSTALL) -m 644 chos.h $(DKMS)
	$(INSTALL) -m 644 dkms.conf $(DKMS)
	$(INSTALL) -m 644 Makefile.dkms $(DKMS)/Makefile


clean: 
	rm -f $(OBJS) *.ko *.o


test:
	/sbin/insmod chos.o
