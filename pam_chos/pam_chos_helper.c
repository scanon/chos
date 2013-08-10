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
 */
#define _GNU_SOURCE

#include "pam_chos.h"
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include <sys/stat.h>
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

int get_chos_info(int argc, const char **argv, int fd, char *user_conf_dir) {

  char osenv[MAXLINE+1];
  char *os;
  const char *env;
  int ret = 0;
  FILE *f;

  pam_chos_config *cfg;
  cfg = init_pam_chos_config();
  parse_pam_chos_args(cfg, argc, argv);

  read_chos_file(cfg->user_conf_file, user_conf_dir, osenv);


  if ((env=getenv("CHOS"))){
    strncpy(osenv,env,MAXLINE);
  }
  else{
    if (osenv[0]==0){
      syslog(LOG_ERR,"CHOS not set\n");
      close(fd);
      return 0;
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

  f = fdopen(fd,"w");
  if(!f) {
    syslog(LOG_ERR, "fdopen() failed: %s\n",strerror(errno));
    return -1;
  }
  syslog(LOG_INFO,"Environment name: %s path: %s \n",osenv, os);
  fputs(os,f);
  fputc('\n',f);
  fputs(osenv,f);
  fputc('\n',f);

  fclose(f);
  return 1;
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
