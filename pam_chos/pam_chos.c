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

#define	PAM_SM_SESSION
#include <security/pam_modules.h>

#include <syslog.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>
#include <pwd.h>
#include <fcntl.h>

#include "pam_chos.h"
#include "../config.h"

int set_multi(char *os);
char * check_chos(char *name);
int read_chos_file(char *dir,char *osenv,int max);

#define	CONFIG	".chos"
#define MAX_LEN 256
#define MAXLINE 80
PAM_EXTERN int pam_sm_open_session(pam_handle_t *pamh, int flags,
				   int argc, const char **argv)
{
  int ret = PAM_SESSION_ERR;
  int onerr = PAM_SUCCESS;
  char const *user;
  struct passwd *pw;
  char *os;
  const char *env;
  char osenv[MAXLINE+1];
  char envvar[50];
  int usedefault=0;
  unsigned int oldeuid;

  oldeuid = geteuid();
  
  openlog("pam_chos", LOG_PID, LOG_AUTHPRIV);
  
  if((ret = pam_get_user(pamh, &user, NULL)) != PAM_SUCCESS) {
    syslog(LOG_ERR, "can't get username: %s", pam_strerror(pamh, ret));
    return ret;
  }
  
  pw=getpwnam(user);
  if (pw->pw_uid==0){
    return PAM_SUCCESS;
  }
  if (pw==NULL){
    syslog(LOG_ERR,"getpwuid failed for %d\n",getuid());
    return ret;
  }

  seteuid(pw->pw_uid);

//  if ((env=pam_getenv(pamh,"CHOS"))){
  if ((env=getenv("CHOS"))){
    strncpy(osenv,env,MAXLINE);
  }
  else{
    read_chos_file(pw->pw_dir,osenv,MAXLINE);
    if (osenv[0]==0){
      syslog(LOG_ERR,"CHOS not set\n");
      return onerr;
    }
  }
  os=check_chos(osenv);

/* We are using the default, but there isn't a default
 *  spec'd in /etc/chos
 */
  if (os==NULL && strcmp(osenv,DEFAULT)==0){
    usedefault=1;  
  }
  else if (os==NULL){
    syslog(LOG_WARNING,"Warning: requested os (%s) is not recognized\n",osenv);
    os=osenv;  /* Let's try it just in case */
  }
  
  if (usedefault==0 && set_multi(os)!=0){
    syslog(LOG_ERR,"Failed to set OS to requested system.\n");
    return ret;
  }

  sprintf(envvar,"CHOS=%.40s",osenv);
  pam_putenv(pamh, envvar);
  seteuid(oldeuid);
  closelog();
  ret = PAM_SUCCESS;
  return ret;
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

int read_chos_file(char *dir,char *osenv,int max)
{
  int conf;
  int i;
  int count=0;
  char userfile[MAX_LEN+1];

  sprintf(userfile,"%.100s/%.5s",dir,CONFIG);
  conf=open(userfile,O_RDONLY);
  if (conf>=0){
    count=read(conf,osenv,MAXLINE-1);
    osenv[count]=0;
    close(conf);
  }
  if (count==0){
    strcpy(osenv,DEFAULT);
    count=7;
  }

  for (i=0;i<count;i++){
    if (osenv[i]=='\n')break;
  }
  osenv[i]=0;
  return count;
}
