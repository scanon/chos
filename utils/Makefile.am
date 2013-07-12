bin_PROGRAMS = chos job_starter pam_job_starter
chos_SOURCES = chos.c chos.h chos_parser.y chos_lexer.l

BUILT_SOURCES = chos_parser.h
AM_YFLAGS = -d

job_starter_SOURCES = job_starter.c
pam_job_starter_SOURCES = pam_job_starter.c
pam_job_starter_LDADD = $(PAM_LIBS) $(PAM_MISC_LIBS)

man_MANS = chos.1

