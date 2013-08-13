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
 * Description:
 * This is a pam module for chos.  It basically does what the chos utility
 * does but inside the pam framework.  It only implements the sessions portion.
 *
 * I looked at some of the other pam modules to understand the basic framework
 * for PAM modules.
 */

/*
 * Here is the order of how things are used
 * - CHOS env
 * - CHOS file
 * - CHOS default
 * - no chos
 */

#define _GNU_SOURCE
#define  PAM_SM_SESSION

#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <limits.h>
#include <pwd.h>
#include <security/pam_modules.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <syslog.h>
#include <unistd.h>

#include "pam_chos.h"
#include "../config.h"




PAM_EXTERN int pam_sm_open_session(pam_handle_t *pamh, int flags,
           int argc, const char **argv)
{
  int ret = PAM_SESSION_ERR;
  int onerr = PAM_SUCCESS;
  char const *user;
  FILE *child_pipe_file;
  struct passwd *pw;
  struct group *gr;
  char env_path[MAXLINE+1];
  char osenv[MAXLINE+1];
  char envvar[50];
  int usedefault=0;
  int child_pid, child_status;
  int child_pipe[2];

  
  openlog("pam_chos", LOG_PID, LOG_AUTHPRIV);
  
  if((ret = pam_get_user(pamh, &user, NULL)) != PAM_SUCCESS) {
    syslog(LOG_ERR, "can't get username: %s", pam_strerror(pamh, ret));
    return ret;
  }
  
  pw=getpwnam(user);

  if (pw==NULL) {
    syslog(LOG_ERR,"getpwuid failed for %d: %s\n",getuid(), strerror(errno));
    return onerr;
  }

  /* UID 0 is excepted from automatic CHOS environment activation */
  if (pw->pw_uid==0){
    return PAM_SUCCESS;
  }

  gr = getgrnam(user);
  if (gr==NULL) {
    syslog(LOG_ERR,"getpwuid failed for %d: %s\n",getuid(), strerror(errno));
    return onerr;
  }

  if(pipe(child_pipe) != 0) {
    syslog(LOG_ERR,"pipe() failed: %s,\n",strerror(errno));
    return onerr;
  }

  child_pid = fork();

  /* Fork failed */
  if (child_pid == -1) {
    syslog(LOG_ERR,"fork() failed: %s\n",strerror(errno));
    close(child_pipe[0]);
    close(child_pipe[1]);
    return onerr;
  }
  else if (child_pid == 0) {
    /* Child process */

    close(child_pipe[0]);

    /* Drop privileges */
    if(setresgid(gr->gr_gid, gr->gr_gid, gr->gr_gid) != 0 ) {
        syslog(LOG_ERR, "setresgid to %d failed: %s", gr->gr_gid, strerror(errno));
        close(child_pipe[1]);
        exit(-1);
    }

    if(setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid) != 0 ) {
        syslog(LOG_ERR, "setresuid to %d failed: %s", gr->gr_gid, strerror(errno));
        close(child_pipe[1]);
        exit(-1);
    }

    syslog(LOG_ERR,"uid, gid, euid, egid: %d, %d, %d, %d\n",
            getuid(), getgid(), geteuid(), getegid());

    ret = get_chos_info(argc, argv, child_pipe[1], pw->pw_dir);
    close(child_pipe[1]);

    if(ret != 1) {
      syslog(LOG_ERR, "Helper code returned status %d\n",ret);
      exit(ret);
    }
    else {
      exit(0);
    }
  }
  /* Parent process */
  else {

    close(child_pipe[1]);

    syslog(LOG_INFO,"forked pid %d\n",child_pid);
    syslog(LOG_ERR,"uid, gid, euid, egid: %d, %d, %d, %d\n",
            getuid(), getgid(), geteuid(), getegid());

    child_pipe_file = fdopen(child_pipe[0],"r");

    if(!child_pipe_file) {
      syslog(LOG_ERR, "fdopen() failed: %s\n",strerror(errno));
      return(onerr);
    }


    if(!(read_line_from_file(child_pipe_file, env_path))) {
      syslog(LOG_ERR, "Failed to read path from child: %s\n",strerror(errno));
      fclose(child_pipe_file);
      wait(&child_status);
      return(onerr);
    }
    
    if(!(read_line_from_file(child_pipe_file, osenv))) {
      syslog(LOG_ERR, "Failed to read environment name from child: %s\n",
              strerror(errno));
      fclose(child_pipe_file);
      wait(&child_status);
      return(onerr);
    }

    fclose(child_pipe_file);
    wait(&child_status);

    if(child_status != 0) {
      syslog(LOG_ERR, "Child returned status: %d\n",child_status);
      return(onerr);
    }

    sanitize_path(env_path, MAXLINE);
    sanitize_name(osenv, MAXLINE);

    if (usedefault==0 && set_multi(env_path)!=0){
      syslog(LOG_ERR,"Failed to set OS to requested system.\n");
      return ret;
    }


    snprintf(envvar,45,"CHOS=%.40s",osenv);
    pam_putenv(pamh, envvar);
    closelog();

    ret = PAM_SUCCESS;
    return ret;
  }
}



PAM_EXTERN int pam_sm_close_session(pam_handle_t *pamh, int flags,
            int argc, const char **argv)
{
  return PAM_SUCCESS;
}

#ifdef PAM_STATIC

/* static module data */

struct pam_module _pam_chos_modstruct = {
  "pam_chos",
  NULL,
  NULL,
  pam_sm_acct_mgmt,
  NULL,
  NULL,
  NULL
};
#endif


/* This writes the target into the chos kernel module */
int set_multi(char *os)
{
  FILE *stream;
  stream=fopen(SETCHOS,"w");
  if (stream==NULL){
    syslog(LOG_ERR,"Unable to open multi root system\n");
    return -3;
  }
  if (fprintf(stream,os)==-1){
    syslog(LOG_ERR,"Unable to write to multi root system\n");
    return -3;
  }
  fclose(stream);
  return 0;
}

