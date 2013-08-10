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

#include <string.h>

#include "pam_chos.h"
#include "../config.h"


void sanitize_name(char *s, int length) {
  sanitize_str(s, length, 0);
  return;
}

void sanitize_path(char *s, int length) {
  sanitize_str(s, length, 1);
  return;
}

void sanitize_str(char *s, int length, int is_path) {
  int i;
  for (i = 0; i < length; i++) {
    if (s[i] == '\n') {
        s[i] = '\0';
    }
    if (!(is_valid_char(s[i], is_path))) {
      s[i]='_';
    }
  }
  return;
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

