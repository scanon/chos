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

#define  PAM_SM_SESSION
#define _GNU_SOURCE

#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <security/pam_modules.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <syslog.h>
#include <unistd.h>

#include "../config.h"
#include "pam_chos.h"




PAM_EXTERN int pam_sm_open_session(pam_handle_t *pamh, int flags,
           int argc, const char **argv)
{

  char const *user;

  char env_path[MAXLINE+1];
  char envvar[MAXLINE+1];
  char osenv[MAXLINE+1];

  int child_pid, child_status;
  int child_pipe[2];
  int ret = PAM_SESSION_ERR;

  struct group const *gr;
  struct passwd const *pw;

  openlog(PAM_CHOS_MODULE_NAME, LOG_PID, LOG_AUTHPRIV);
  
  /* Retrieve user information from PAM */
  if((ret = pam_get_user(pamh, &user, NULL)) != PAM_SUCCESS) {
    syslog(LOG_ERR, "pam_get_user failed: %s\n", pam_strerror(pamh, ret));
    return(ONERR);
  }
  
  /* Retrieve the passwd structure for this user. */
  errno = 0;
  pw = getpwnam(user);
  if (pw==NULL) {
    syslog(LOG_ERR,"getpwnam failed for %d: %s\n",getuid(), strerror(errno));
    return(ONERR);
  }

  /* UID 0 is excepted from automatic CHOS environment activation.
   * Immediately stop processing for UID 0. */
  if (pw->pw_uid==0){
    return(PAM_SUCCESS);
  }

  /* Create a pipe for communication with our child process. */
  if(pipe(child_pipe) != 0) {
    syslog(LOG_ERR,"pipe() failed: %s,\n",strerror(errno));
    return(ONERR);
  }

  /* Fork a child.  This child will drop privileges and determine the
   * appropriate CHOS environment to set. */
  child_pid = fork();

  if (child_pid == -1) {
    /* Fork failed */
    syslog(LOG_ERR,"fork() failed: %s\n",strerror(errno));

    close_fd(child_pipe[0]);
    close_fd(child_pipe[1]);

    return(ONERR);
  }
  else if (child_pid == 0) {
    /* Child process */
    if(!(close_fd(child_pipe[0]) == 0)) {
      exit(-1);
    }

    /* Retrieve the group structure for this user. */
    errno = 0;
    gr = getgrnam(user);
    if (gr==NULL) {
      syslog(LOG_ERR,"getgrnam failed for %d: %s\n",getuid(), strerror(errno));
      close_fd(child_pipe[1]);
      exit(-1);
    }

    /* Drop privileges */
    if( setgroups(1, &(gr->gr_gid)) != 0) {
      syslog(LOG_ERR, "setgroups() failed: %s", strerror(errno));
      close_fd(child_pipe[1]);
      exit(-1);
    }
    if(setresgid(gr->gr_gid, gr->gr_gid, gr->gr_gid) != 0 ) {
      syslog(LOG_ERR, "setresgid to %d failed: %s", gr->gr_gid, strerror(errno));
      close_fd(child_pipe[1]);
      exit(-1);
    }

    if(setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid) != 0 ) {
      syslog(LOG_ERR, "setresuid to %d failed: %s", gr->gr_gid, strerror(errno));
      close_fd(child_pipe[1]);
      exit(-1);
    }

#ifdef PAM_CHOS_DEBUG
    syslog(LOG_DEBUG,"uid, gid, euid, egid: %d, %d, %d, %d\n",
            getuid(), getgid(), geteuid(), getegid());
#endif


    /* Retrive the correct CHOS environment name and path, write
     * it to child_pipe.  get_chos_info will also close child_pipe.
     */
    if( (ret = get_chos_info(argc, argv, child_pipe[1], pw->pw_dir)) != 1) {
      syslog(LOG_ERR, "get_chos_info() returned status %d\n",ret);
      exit(-1);
    }
    else {
      exit(0);
    }
  }
  else {
    /* Parent process */
    if(!(close_fd(child_pipe[1]) == 0)) {
      wait(&child_status);
      return(ONERR);
    }

#ifdef PAM_CHOS_DEBUG
    syslog(LOG_DEBUG,"forked pid %d\n",child_pid);
    syslog(LOG_DEBUG,"uid, gid, euid, egid: %d, %d, %d, %d\n",
            getuid(), getgid(), geteuid(), getegid());
#endif


    /* Retrieve values for env_path and osenv from child process. */
    ret = retrieve_from_child(env_path, osenv, child_pipe[0]);
    wait(&child_status);

    if(ret != 1) {
      syslog(LOG_ERR, "Information retrieval from child returned status: %d\n",ret);
      return(ONERR);
    }

    if(child_status != 0) {
      syslog(LOG_ERR, "Child returned status: %d\n",child_status);
      return(ONERR);
    }


    /* Set the CHOS link to env_path */
    sanitize_path(env_path, MAXLINE+1);
    sanitize_name(osenv, MAXLINE+1);

    if (set_multi(env_path) != 0){
      syslog(LOG_ERR,"Failed to set OS to requested system.\n");
      return(ONERR);
    }


    /* Set $CHOS to the environment name */
    ret = snprintf(envvar,MAXLINE+1,"CHOS=%.40s",osenv);
    if (ret < 1) {
      syslog(LOG_ERR, "Could not construct CHOS environment variable definition.\n");
      return(ONERR);
    }
    else if (ret > MAX_LEN+1) {
      syslog(LOG_ERR, "Environment variable definition is too long.\n");
      return -1;
    }

    if( (ret = pam_putenv(pamh, envvar)) != PAM_SUCCESS) {
      syslog(LOG_ERR, "pam_putenv() failed: %s\n", pam_strerror(pamh, ret));
    }

    closelog();

    return(PAM_SUCCESS);
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


