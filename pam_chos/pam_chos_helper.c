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

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include <sys/stat.h>
#include "../config.h"
#include "pam_chos.h"

pam_chos_config *init_pam_chos_config(pam_chos_config *cfg) {

  if(!cfg) {
    return NULL;
  }

  cfg->fail_to_default = FAIL_TO_DEFAULT;

  strncpy(cfg->user_conf_file, USER_CONF_FILE_DEFAULT, MAX_LEN+1);
  cfg->user_conf_file[MAX_LEN] = '\0';

  return cfg;
}

int argmatch(const char *arg, const char *match) {
  if(strlen(arg) < strlen(match)+1) {
    /* The length of the potential match is too short to include a
     * value
     */
    return 0;
  }
  else {
    return (strncmp(arg, match, strlen(match)) == 0);
  }
}

int parse_pam_chos_args(pam_chos_config *cfg, int argc, const char
        **argv) {

  char val;

  while(argc--) {

    if(argmatch(argv[0],"user_conf_file=")) {
        strncpy(cfg->user_conf_file,
                argv[0]+strlen("user_conf_file="),
                MAX_LEN+1);
        cfg->user_conf_file[MAX_LEN] = '\0';
      if(!(cfg->user_conf_file)) {
        syslog(LOG_ERR,"Failed to parse value for user_conf_file\n");
      }
    }

    if(argmatch(argv[0],"fail_to_default=")) {
      if( (val = *(argv[0]+strlen("fail_to_default="))) ) {
        switch(val)  {
          case '0':
            cfg->fail_to_default = 0;
            break;
          case '1':
            cfg->fail_to_default = 1;
            break;
          default:
            syslog(LOG_ERR,"Invalid value for fail_to_default.\n");
            return -1;
        }
      }
      else {
        syslog(LOG_ERR, "Failed to parse arguments.\n");
        return -1;
      }
    }

    argv++;
  }
  return 1;
}

int get_chos_info(int argc, const char **argv, int fd, char *user_conf_dir) {

  char osenv[MAXLINE+1];
  char *os;
  const char *env;
  FILE *f;
  pam_chos_config cfg;

  if(!(init_pam_chos_config(&cfg))) {
    syslog(LOG_ERR, "Failed to initialize configuration\n");
    return -1;
  }

  if(parse_pam_chos_args(&cfg, argc, argv) != 1) {
    syslog(LOG_ERR, "Failed to parse configuration\n");
    return -1;
  }

  if( (read_chos_file(cfg.user_conf_file, user_conf_dir, osenv)) < 0) {
    syslog(LOG_ERR, "Error while handling user configuration file\n");
    return -1;
  }

  /* If $CHOS is set, use its value as the OS name. */
  if ((env=getenv("CHOS"))){
    if ( strlen(env) > MAXLINE ) {
      syslog(LOG_WARNING, "Warning: $CHOS value is too long, ignoring\n");
    }
    else {
      strncpy(osenv,env,MAXLINE+1);
      sanitize_name(osenv, strlen(osenv));
    }
  }


  /*
   * Attempt to retrieve the environment path from the environment
   * name. 
   * */
  os=check_chos(osenv);


  if (os == NULL) {
    /* We were unable to match the environment name against a valid
     * environment path. */

    if ( (strcmp(osenv,DEFAULT_ENV_NAME)==0) || 
       (cfg.fail_to_default > 0) ) {
      /* If the environment name is "default", or fail_to_default==1,
       * then fail back to the default environment. */
      syslog(LOG_WARNING,
        "Warning: requested os (%s) is not recognized; using default (fail_to_default=%d).\n",
        osenv, cfg.fail_to_default);
      os=check_chos(DEFAULT_ENV_NAME);
    }

    else {
      /* Try the (likely invalid) environment name. */
      syslog(LOG_WARNING,
              "Warning: requested os (%s) is not recognized.\n",osenv);
      os=osenv;
    }
  }


  /* Write the environment name and path to fd */
  f = fdopen(fd,"w");
  if(!f) {
    syslog(LOG_ERR, "fdopen() failed: %s\n",strerror(errno));
    return -1;
  }

  /* Write the os path and name to the file. */
  fputs(os,f);
  fputc('\n',f);
  fputs(osenv,f);
  fputc('\n',f);

  /* We are done writing to the pipe, so we can close it now. */
  if(fclose(f) != 0) {
    syslog(LOG_ERR, "fclose() failed: %s\n",strerror(errno));
    return -1;
  }

  return 1;
}

char *check_chos(char *name)
{
  FILE *cfile;
  static char buffer[MAXLINE+1];
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
    path=fgets(buffer,MAXLINE+1,cfile);
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

ssize_t read_chos_file(char *user_conf_file, char *dir, char *osenv)
{
  int conf;
  int i;
  ssize_t count = 0;
  int ret;
  char userfile[MAX_LEN+1];

  /* Build the full path to the user configuration file. */
  ret = snprintf(userfile,MAX_LEN+1,"%.100s/%.20s",dir,user_conf_file);

  if (ret < 1) {
    syslog(LOG_ERR, "Could not construct path to user configuration file.\n");
    return -1;
  }
  else if (ret > MAX_LEN+1) {
    syslog(LOG_ERR, "Path to user configuration file is too long.\n");
    return -1;
  }

  /*
   * Open the user configuration file and read the contents into
   * osenv.  osenv must have at least MAXLINE+1 of space available.
   * 
   * If no bytes or read or the configuration file cannot be opened,
   * write the default environment name instead.
   */
  conf=open(userfile,O_RDONLY);

  if (conf >= 0) {
    /* Read environment name from user configuration file. */
    count=read(conf,osenv,MAXLINE);

    if (count == 0) {
      /* EOF was received and no bytes were read.  Log a warning and
       * continue. */
      syslog(LOG_WARNING,
        "Warning: Read 0 bytes from user configuration file (%s)\n",
         userfile);
    }
    else if (count < 0) {
      /* read() returned an error.  Log a warning and confinue. */
      syslog(LOG_WARNING,
        "Warning: failed to read() user configuration file (%s): %s\n",
        userfile, strerror(errno));
        count = 0;
    }

    /* Explicitly set osenv[count] to the null byte.  On error or EOF,
     * this sets osenv[0] to '\0'.  On successful reads, it ensures
     * the string is null-terminated. */
    osenv[count] = '\0';

    if (close(conf) != 0) {
      syslog(LOG_WARNING,
        "Could not close() user configuration file (%s): %s\n",
        userfile, strerror(errno));
      return(-1);
    }
  }
  else if(errno != ENOENT) {
    /* The user configuration file could not be opened.  Log a warning
     * if open() failed for a reason other than ENOENT, and continue.
     */
    syslog(LOG_WARNING,
      "Warning: Could not open user configuration file (%s): %s\n",
      userfile, strerror(errno));
  }

  if (count == 0) {
    /* If no bytes were read, then set osenv to the default
     * environment name.
     */
    snprintf(osenv, MAXLINE+1, "%s", DEFAULT_ENV_NAME);
    count=strlen(osenv) + 1;
  }

  for (i = 0; i < count; i++){
    if ((osenv[i]=='\n') || (osenv[i]=='\0') )
      break;
  }

  osenv[i]=0;

  sanitize_name(osenv,strlen(osenv));

  return count;
}
