#
#

include ../Make.Rules

TITLE=pam_chos

LIBFILES = $(TITLE)
LIBSRC = $(addsuffix .c,$(LIBFILES))
LIBOBJ = $(addsuffix .o,$(LIBFILES))

LINK_PAMMODUTILS = -lpam
INCLUDE_PAMMODUTILS = -Ipam/include

LIBSHARED = $(TITLE).so
MANPAGE = $(TITLE).8

all: $(LIBSHARED)

$(LIBOBJD): $(LIBSRC)

$(LIBSHARED):   $(LIBOBJ)
	$(LD_D) -o $@ $(LIBOBJ) $(MODULE_SIMPLE_EXTRALIBS) $(NEED_LINK_LIB_C) $(LINK_PAMMODUTILS)

install: all
	$(MKDIR) $(FAKEROOT)$(SECUREDIR)
	$(INSTALL) -m $(SHLIBMODE) $(LIBSHARED) $(FAKEROOT)$(SECUREDIR)
	$(INSTALL) -m 644 $(MANPAGE) $(FAKEROOT)$(MAN8DIR)

remove:
	rm -f $(FAKEROOT)$(SECUREDIR)/$(TITLE).so

clean:
	rm -f $(LIBOBJD) core *~
	rm -f *.a *.o *.so *.bak

.c.o:
	$(CC) $(CFLAGS) $(PIC) -c $<


