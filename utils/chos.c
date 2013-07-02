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
#include <stdio.h>
#include <stdlib.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>

#include "chos.h"
#include "../config.h"


#define MAXLINE 80

int main(int argc, char *argv[]) {
   struct passwd *pw;
   FILE *stream;
   pid_t pid;
   char *os;
   char *osenv;
   char chos[MAX_OS];
   char *newarg[3];
   char **newenv;

   if(!(chos_parse_args(argc, argv))) {
     return 1;
   }

   osenv=getenv("CHOS");
   if (osenv==NULL){
     fprintf(stderr,"CHOS not set\n");
     return -2;
   }
   os=check_chos(osenv);
   if (os==NULL && strcmp(osenv,DEFAULT)){
     fprintf(stderr,"Warning: The requested os is not recognized.  Trying anyway.\n");
     os=osenv;
   }

   pw=getpwuid(getuid());
   if (pw==NULL){
     fprintf(stderr,"getpwuid failed for %d\n",getuid());
     return -2;
   }

   if (os && get_multi(chos)!=0){
     fprintf(stderr,"Failed to read the chroot OS system link.\nPerhaps the system isn't fully configured.\n");
     return -3;
   }

/* If the current chos is different from the requested chos then update it */
   if (os && strncmp(chos,os,MAX_OS)!=0 && set_multi(os)!=0){
     fprintf(stderr,"Failed to set chroot OS system link.\nPerhaps there is a permission problem.\n");
     return -3;
   }

   setuid(getuid());
   if (argc==1){
      newenv=set_env();
      if (newenv==NULL){
        fprintf(stderr,"Failed to initialize environment for login\n");
        return -1;
      }
      newarg[0]=pw->pw_shell;
      if (strstr(pw->pw_shell,"csh")){
        newarg[1]="-l";
      }
      else{
        newarg[1]="-";
      }
      newarg[2]=NULL;
      execve(pw->pw_shell,newarg,newenv);
    }
    else{
      execvp(argv[1],&argv[1]);
    }
}

int configure_chos() {
}

int is_chrooted(char *chos) {
/*
 * Check to see if we are already chrooted.  This is an indirect test.
 *  We look to see if the current chos (still stored in chos) is set to
 *  the default value of /.
 */
  if (chos[0]=='/' && chos[1]==0 )
    return 0;
  else
    return 1;
}

/*
 * Simple function to set the chos link
 */
int set_multi(char *os) {
   FILE *stream;
   stream=fopen(SETCHOS,"w");
   if (stream==NULL){
     fprintf(stderr,"Unable to open multi root system\n");
     return -3;
   }
   if (fprintf(stream,os)==-1){
     fprintf(stderr,"Unable to write to multi root system\n");
     return -3;
   }
   fclose(stream);
   return 0;
}

/*
 * Simple function to get the target of the chos link
 */
int get_multi(char *os) {
   int len;
   len=readlink(LINKCHOS,os,MAX_OS);
   if (len<1){
     return -3;
   }
   else{
     os[len]=0;
   }
   return 0;
}

/*
 * This function looks at the /etc/chos.conf file and sets variables
 * listed in the %ENV section based on the environment of the calling shell.
 */
char ** set_env() {
  FILE *stream;
  char **env;
  char *value;
  char *var;
  char buffer[MAXLINE];
  int ret=0;
  int count=1;
  int setc=0;
  int start=0;
  int len;

  stream=fopen(CHOSENV,"r");
  if (!stream){

    fprintf(stderr,"Unable to open conf file (%s)\n",CHOSENV);
    perror("chos.conf");
    return NULL; 
  }

/* We need to count the number of variables first. */
  start=0;
  while(1){
    value=fgets(buffer,MAXLINE,stream);
    if (value==NULL)
      break;
    if (buffer[0]=='#' || buffer[0]=='\n')
      continue; 
/* Remove new line */
    while(*value!=0 && *value!='\n')
  value++;
    *value=0;
    if (start){
      count++;
      if (buffer[0]=='%')
  break;
    }
    else if (strcmp(buffer,ENVHEAD)==0){
      start=1;
    }
  }

/* Allocate an array of pointers */
  env=(char **)malloc(sizeof(char *)*count);
  if (env==NULL){
    fprintf(stderr,"Failed to allocate memory for env\n");
    return NULL;
  }
  rewind(stream);
  start=0;
  while(1){
    value=fgets(buffer,MAXLINE,stream);
    if (value==NULL)
      break;
    if (buffer[0]=='#' || buffer[0]=='\n')
      continue; 
/* Remove new line */
    while(*value!=0 && *value!='\n')
  value++;
    *value=0;
    if (start){
      count++;
      if (buffer[0]=='%')
  break;
      value=getenv(buffer);
      if (strcmp(buffer,"PATH")==0){
        env[setc]=DEFPATH;
        setc++;
      }
      else if (value!=NULL && setc<count){
        len=strlen(buffer)+strlen(value)+2;
        var=(char *)malloc(len);     /* Allocate space for entry */
        if (var==NULL){
          fprintf(stderr,"failed to allocate memory\n");
          return;
        }
        sprintf(var,"%s=%s",buffer,value);
        env[setc]=var;
        setc++;
      }
    }
    else if (strcmp(buffer,ENVHEAD)==0){
      start=1;
    }
  }
  if (setc<count){
    env[setc]=NULL;
  }
  else{
    fprintf(stderr,"Unable to terminate environment\n");
    return NULL;
  }
  fclose(stream);
  return env; 
}

char * check_chos(char *name) {
  FILE *cfile;
  static char buffer[MAXLINE];
  struct stat st;
  char *path;
  char *retpath=NULL;
  int ret=0;
  int count=1;
  int setc=0;
  int start=0;
  int len;
  int han;

  cfile=fopen(CHOSCONF,"r");
  if (cfile==NULL){
    fprintf(stderr,"Error opening config file %s\n",CHOSCONF);
    return NULL;
  }
  han=fileno(cfile);
  if (fstat(han,&st)!=0){
    fprintf(stderr,"Error accessing config file %s\n",CHOSCONF);
    return NULL;
  }
  else if (st.st_uid!=0){
    fprintf(stderr,"Error: %s must be owned by root\n",CHOSCONF);
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

int argmatch(const char *arg, const char *match) {
  if(strlen(arg) < strlen(match)) {
    return 0;
  }
  else {
    return (strncmp(arg, match, strlen(match)) == 0);
  }
}

void chos_print_version(void) {
  printf("CHOS version %s\n",VERSION);
}

void chos_print_usage(void) {
  printf("Usage: chos [OPTION]\n"
         "       chos [COMMAND]\n\n"
         "The \"chos\" utility will run COMMAND within the CHOS\n"
         "environment named within the $CHOS environment variable.\n\n"
         "If no command is provided, a new login shell will be launched.\n\n"
         "If the only argument to chos is exactly one of the options defined\n"
         "below, then the following behavior will result:\n\n"
         "Options:\n"
         /* "       --avail\t\t\tList available CHOS environments\n" TODO */
         "       --version\t\tPrint version information\n"
         "       --help\t\t\tPrint this help message\n\n");
}

int chos_parse_args(int argc, char **argv) {
  if (argc != 2) {
    return 1;
  }

  if(argmatch(argv[1], "--version")) {
    chos_print_version();
    return 0;
  }
  else if(argmatch(argv[1], "--help")) {
    chos_print_version();
    chos_print_usage();
    return 0;
  }

  return 1;
}
