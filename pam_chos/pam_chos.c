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
#include <security/pam_modules.h>

#include <syslog.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <grp.h>
#include <pwd.h>
#include <fcntl.h>

#include "pam_chos.h"
#include "../config.h"


pam_chos_config *init_pam_chos_config(void) {

  pam_chos_config pam_chos_config_default = {
    .user_conf_file = strdup(USER_CONF_FILE_DEFAULT),
    .fail_to_default = 0
  };

  pam_chos_config *cfg =
    (pam_chos_config *)malloc(sizeof(pam_chos_config));

  return memcpy(cfg, &pam_chos_config_default,
    sizeof(pam_chos_config));
}

int argmatch(const char *arg, const char *match) {
  if(strlen(arg) < strlen(match)+1) {
    return 0;
  }
  else {
    return (strncmp(arg, match, strlen(match)) == 0);
  }
}

void parse_pam_chos_args(pam_chos_config *args, int argc, const char
        **argv) {


  while(argc--) {
    if(argmatch(argv[0],"user_conf_file=")) {
      args->user_conf_file =
        strndup(argv[0]+strlen("user_conf_file="),MAX_LEN);
    }

    if(argmatch(argv[0],"fail_to_default=")) {
      args->fail_to_default =
        atoi(argv[0]+strlen("fail_to_default="));
    }
    argv++;
  }
}

PAM_EXTERN int pam_sm_open_session(pam_handle_t *pamh, int flags,
           int argc, const char **argv)
{
  int ret = PAM_SESSION_ERR;
  int onerr = PAM_SUCCESS;
  char const *user;
  FILE *f;
  struct passwd *pw;
  int gid;
  char *os;
  char env_path[MAXLINE+1];
  const char *env;
  char osenv[MAXLINE+1];
  char envvar[50];
  int usedefault=0;
  int child_pid, child_status;
  int fd[2];
  pam_chos_config *cfg;

  
  openlog("pam_chos", LOG_PID, LOG_AUTHPRIV);
  
  if((ret = pam_get_user(pamh, &user, NULL)) != PAM_SUCCESS) {
    syslog(LOG_ERR, "can't get username: %s", pam_strerror(pamh, ret));
    return ret;
  }
  
  pw=getpwnam(user);
  gid = getgrnam(user)->gr_gid;
  if (pw->pw_uid==0){
    return PAM_SUCCESS;
  }
  if (pw==NULL){
    syslog(LOG_ERR,"getpwuid failed for %d\n",getuid());
    return ret;
  }

  pipe(fd);
  child_pid = fork();

  /* Fork failed */
  if (child_pid == -1) {
    syslog(LOG_ERR,"failed to fork(): %s\n",strerror(errno));
    close(fd[0]);
    close(fd[1]);
    return child_pid;
  }
  else if (child_pid == 0) {
    /* Child process */

    close(fd[0]);
    //dup2(fd[1], STDOUT_FILENO);
    //close(fd[1]);


    /* Drop privileges */
    if(setresgid(gid, gid, gid) != 0 ) {
        syslog(LOG_ERR, "setresgid failed: %s", strerror(errno));
        close(fd[1]);
        exit(-1);
    }

    if(setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid) != 0 ) {
        syslog(LOG_ERR, "setresuid failed: %s", strerror(errno));
        close(fd[1]);
        exit(-1);
    }

    syslog(LOG_ERR,"uid, gid, euid, egid: %d, %d, %d, %d\n",
            getuid(), getgid(), geteuid(), getegid());

    cfg = init_pam_chos_config();
    parse_pam_chos_args(cfg, argc, argv);

    read_chos_file(cfg->user_conf_file, pw->pw_dir, osenv);

    if ((env=getenv("CHOS"))){
      strncpy(osenv,env,MAXLINE);
    }
    else{
      if (osenv[0]==0){
        syslog(LOG_ERR,"CHOS not set\n");
        close(fd[1]);
        exit(onerr);
      }
    }
    os=check_chos(osenv);

    free(cfg->user_conf_file);
    free(cfg);

    if (os == NULL) {
      if ( (strcmp(osenv,DEFAULT_ENV_NAME)==0) || 
         (cfg->fail_to_default > 0) ) {
        /* Fail back to the default CHOS */
        syslog(LOG_WARNING,
          "Warning: requested os (%s) is not recognized; using default (fail_to_default=%d).\n",
          osenv, cfg->fail_to_default);
        os=check_chos(strdupa(DEFAULT_ENV_NAME));
      }
      else {
        /* Try the (likely invalid) environment name. */
        syslog(LOG_WARNING,
                "Warning: requested os (%s) is not recognized.\n",osenv);
        os=osenv;
      }
    }

    f = fdopen(fd[1],"w");
    if(!f) {
      syslog(LOG_ERR, "fdopen() failed: %s\n",strerror(errno));
      exit(-1);
    }
    syslog(LOG_INFO,"Environment name: %s path: %s \n",osenv, os);
    fputs(os,f);
    fputc('\n',f);
    fputs(osenv,f);
    fputc('\n',f);

    fclose(f);
    exit(0);
  }
  /* Parent process */
  else {

    close(fd[1]);
    syslog(LOG_INFO,"forked pid %d\n",child_pid);
    syslog(LOG_ERR,"uid, gid, euid, egid: %d, %d, %d, %d\n",
            getuid(), getgid(), geteuid(), getegid());

    f = fdopen(fd[0],"r");

    if(!f) {
      syslog(LOG_ERR, "fdopen() failed: %s\n",strerror(errno));
      return(onerr);
    }


    if(!(read_line_from_fd(f, env_path))) {
      syslog(LOG_ERR, "Failed to read path from child: %s\n",strerror(errno));
      fclose(f);
      wait(&child_status);
      return(onerr);
    }
    
    if(!(read_line_from_fd(f, osenv))) {
      syslog(LOG_ERR, "Failed to read environment name from child: %s\n",
              strerror(errno));
      fclose(f);
      wait(&child_status);
      return(onerr);
    }

    fclose(f);
    wait(&child_status);

    if(child_status != 0) {
      syslog(LOG_ERR, "Child returned status: %s\n",child_status);
      return(onerr);
    }

    sanitize_path(env_path, MAXLINE);
    sanitize_name(osenv, MAXLINE);
    syslog(LOG_INFO, "Activate child environment: %s at %s\n",osenv,env_path);

  
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

/* Read a line of up to MAXLINE-1 from an open FD */
/* Sufficient storage must be available at *dest */
char *read_line_from_fd(FILE *f, char *dest) {
  return fgets(dest, MAXLINE, f);
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


/* This looks up the path for the alias specified in the users chos file.
 */
char * check_chos(char *name)
{
  FILE *cfile;
  static char buffer[MAXLINE];
  struct stat st;
  char *path;
  char *retpath=NULL;
  int start=0;
  int han;

  cfile=fopen(CHOSCONF,"r");
  if (cfile==NULL){
    syslog(LOG_ERR,"Error opening config file %s\n",CHOSCONF);
    return NULL;
  }
  han=fileno(cfile);
  if (fstat(han,&st)!=0){
    syslog(LOG_ERR,"Error accessing config file %s\n",CHOSCONF);
    return NULL;
  }
  else if (st.st_uid!=0){
    syslog(LOG_ERR,"Error: %s must be owned by root\n",CHOSCONF);
    return NULL;
  }
  start=0;
  while(retpath==NULL){
    path=fgets(buffer,MAXLINE,cfile);
    if (path==NULL)
      break;
    if (buffer[0]=='#' || buffer[0]=='\n')
      continue;
/* Remove new line */
    while(*path!=0 && *path!='\n')
        path++;
    *path=0;
    if (start){
      if (buffer[0]=='%')
        break;
      path=buffer;
      while (*path!=':' && *path!=0){
        path++;
      }
      if (*path==0){
/*        fprintf(stderr,"Invalid line in chos config file: %s.\n",buffer); */
        continue;
      }
      *path=0;
      path++;
      if (strcmp(buffer,name)==0){
        retpath=path;
        break;
      }
    }
    else if (strcmp(buffer,SHELLHEAD)==0){
      start=1;
    }
  }
  fclose(cfile);
  return retpath;
}

int read_chos_file(char *user_conf_file, char *dir, char *osenv)
{
  int conf;
  int i;
  int count=0;
  char userfile[MAX_LEN+1];

  snprintf(userfile,MAX_LEN,"%.100s/%.20s",dir,user_conf_file);
  conf=open(userfile,O_RDONLY);
  if (conf>=0){
    count=read(conf,osenv,MAXLINE-1);
    osenv[count]=0;
    close(conf);
  }
  else {
    syslog(LOG_WARNING,
      "Warning: Could not open user configuration file (%s): %s\n",
      userfile, strerror(errno));
  }
  if (count==0) {
    snprintf(osenv, MAXLINE-1, "%s", DEFAULT_ENV_NAME);
    count=strlen(osenv) + 1;
  }

  for (i=0;i<count;i++){
    if ((osenv[i]=='\n') || (osenv[i]=='\0') )
      break;
  }
  sanitize_name(osenv,count);

  osenv[i]=0;


  return count;
}

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
