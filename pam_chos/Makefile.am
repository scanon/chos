securedir = ${FAKEROOT}$(SECUREDIR)
secure_LTLIBRARIES = pam_chos.la
pam_chos_la_SOURCES = pam_chos.c pam_chos_helper.c pam_chos_util.c
pam_chos_la_LIBADD = $(PAM_LIBS)
pam_chos_la_LDFLAGS = -module -avoid-version -shared
pam_chos_la_CFLAGS = -Wall -Wformat -Werror -pedantic -Wuninitialized -Wtype-limits

man_MANS = pam_chos.8

