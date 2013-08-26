/*
 * CHOS (c) 2004, The Regents of the University of California, through
 * Lawrence Berkeley National Laboratory (subject to receipt of any
 * required approvals from the U.S. Dept. of Energy).  All rights
 * reserved.
 *
 * If you have questions about your rights to use or distribute this
 * software, please contact Berkeley Lab's Technology Transfer
 * Department at  TTD@lbl.gov referring to "CHOS (LBNL Ref CR-2025)"
 *
 * NOTICE.  This software was developed under funding from the U.S.
 * Department of Energy.  As such, the U.S. Government has been granted
 * for itself and others acting on its behalf a paid-up, nonexclusive,
 * irrevocable, worldwide license in the Software to reproduce, prepare
 * derivative works, and perform publicly and display publicly.
 * Beginning five (5) years after the date permission to assert
 * copyright is obtained from the U.S. Department of Energy, and subject
 * to any subsequent five (5) year renewals, the U.S. Government is
 * granted for itself and others acting on its behalf a paid-up,
 * nonexclusive, irrevocable, worldwide license in the Software to
 * reproduce, prepare derivative works, distribute copies to the public,
 * perform publicly and display publicly, and to permit others to do so.
 *
 */

#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>

#include "pam_chos.h"
#include "../config.h"


int sanitize_name(char *s, int length) {
  return sanitize_str(s, length, 0);
}

int sanitize_path(char *s, int length) {
  return sanitize_str(s, length, 1);
}

int close_fd(int fd) {
  int ret;
  ret = close(fd);

  if(ret != 0) {
    syslog(LOG_ERR,"close() failed: %s\n",strerror(errno));
  }

  return ret;
}

int sanitize_str(char *s, int length, int is_path) {
  int was_clean = 1;
  int i;
  for (i = 0; i < length; i++) {
    if (s[i] == '\n') {
        s[i] = '\0';
    }
    if (!(is_valid_char(s[i], is_path))) {
      s[i]='_';
      was_clean = 0;
    }
  }
  return was_clean;
}

int is_valid_char(char c, int is_path) {
  int is_valid = 0;

  /* Allow [0-9a-zA-Z-._] and '/' if is_path is true */
  if  (
        (c == '\0') ||
        (c == '-') ||
        (c == '.') ||
        ((c >= '0') && (c <= '9')) ||
        ((c >= '/') && (is_path)) ||
        ((c >= 'a') && (c <= 'z')) ||
        (c == '_') ||
        ((c >= 'A') && (c <= 'Z')) 
      ) is_valid = 1;

  /* syslog(LOG_ERR,"char %c,%d read %d\n",c,c,is_valid); */
  return is_valid;
}

char *read_line_from_file(FILE *f, char *dest) {
  return fgets(dest, MAXLINE, f);
}

int retrieve_from_child(char *env_path, char *osenv, int fd) {

  FILE *child_pipe_file;
  char *dest[3];
  char **i;
  int ret = 1;

  dest[0] = env_path;
  dest[1] = osenv;
  dest[2] = NULL;

  if(!(child_pipe_file = fdopen(fd,"r"))) {
    syslog(LOG_ERR, "fdopen() failed: %s\n",strerror(errno));
    return(-1);
  }

  for (i = dest; *i != NULL; i++) {
    if(!(read_line_from_file(child_pipe_file, *i))) {
      syslog(LOG_ERR, "Failed to read line from child.\n");
      ret = -1;
      break;
    }
  }

  if(fclose(child_pipe_file) != 0) {
    syslog(LOG_ERR, "fclose() failed: %s\n",strerror(errno));
    ret = -1;
  }

  return(ret);
}

/* This writes the target into the chos kernel module */
int set_multi(char *os)
{
  FILE *stream;
  stream=fopen(SETCHOS,"w");
  if (stream==NULL){
    syslog(LOG_ERR,"Unable to open multi root system: %s\n",
      strerror(errno));
    return -3;
  }
  if (fprintf(stream,os)==-1){
    syslog(LOG_ERR,"Unable to write to multi root system: %s\n",
      strerror(errno));
    return -3;
  }
  fclose(stream);
  return 0;
}

