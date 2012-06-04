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

#include <stdio.h>
#include <stdlib.h>
#include <linux/sched.h>
#include <pwd.h>
#include <sys/mount.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>

#include "chos.h"
#include "../config.h"

int set_multi();
char * check_chos(char *name);
char ** set_env();

#define MAXLINE 80


int main(int argc, char *argv[])
{
   struct passwd *pw;
   FILE *stream;
   pid_t pid;
   char *os;
   char *osenv;
   char chos[MAX_OS];
   char *newarg[3];
   char **newenv;

   osenv=getenv("CHOS");
   if (osenv==NULL){
     fprintf(stderr,"CHOS not set\n");
     return -2;
   }
   os=check_chos(osenv);
   if (os==NULL && strcmp(osenv,DEFAULT)){
     fprintf(stderr,"Error: The requested os is not recognized.  Aborting.\n");
     return 1;
   }

   pw=getpwuid(getuid());
   if (pw==NULL){
     fprintf(stderr,"getpwuid failed for %d\n",getuid());
     return -2;
   }

/* If the current chos is different from the requested chos then update it */
   if (os && strncmp(chos,os,MAX_OS)!=0 && set_multi(os)!=0){
     fprintf(stderr,"Failed to set chroot OS system link.\nPerhaps there is a permission problem.\n");
     return -3;
   }

   if(setuid(getuid())!=-0) {
       perror("setuid");
       return(1);
   }

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

int configure_chos()
{
}

int is_chrooted(char *chos)
{
/* Check to see if we are already chrooted.  This is an indirect test.
 *  We look to see if the current chos (still stored in chos) is set to
 *  the default value of /.
 */
  if (chos[0]=='/' && chos[1]==0 )
    return 0;
  else
    return 1;
}

/* Simple function to set the chos link
 */
int set_multi(char *os)
{
  if((unshare(CLONE_NEWNS)!=0)) {
    perror("clone");
    return(1);
  }

  if((mount_dirs(local_dirs,"/local") || mount_dirs(chos_dirs,os))!=0) {
    perror("mount_dirs");
    return 1;
  }

  if(chroot(chos_root)!=0) {
    perror("chroot");
    return 1;
  }

   fprintf(stderr,"Chroot complete.\n");
   return 0;
}

int mount_dirs(char **chos_dir, char *prefix) {

  do {
    int src_len = strlen(*chos_dir)+strlen(prefix)+1;
    int target_len = strlen(*chos_dir)+strlen(chos_root)+1;

    char *src_path = (char *)malloc(sizeof(char)*(src_len+1));
    char *target_path = (char *)malloc(sizeof(char)*(target_len+1));

    snprintf(src_path,src_len+1,"%s/%s",prefix,*chos_dir);
    snprintf(target_path,target_len+1,"%s/%s",chos_root,*chos_dir);

    fprintf(stderr,"%s on %s\n",src_path,target_path);

    /* TODO: MS_NODEV and MS_NOSUID do not appear to work here */
    if(mount(src_path,target_path,"bind", MS_BIND|MS_NODEV|MS_NOSUID|MS_RDONLY,NULL)!=0) {
       perror("mount");
       return 1;
     }

    free(src_path);
    free(target_path);

    chos_dir+=1;
  } while (*chos_dir);

  return 0;

}


/*
 * This function looks at the /etc/chos.conf file and sets variables
 * listed in the %ENV section based on the environment of the calling shell.
 */
char ** set_env()
{
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
/*        printf("%s=%s\n",buffer,value); */
        sprintf(var,"%s=%s",buffer,value);
        env[setc]=var;
        setc++;
      }
/*      else{
 *       fprintf(stderr,"var %s not set\n",buffer);
 *     }
 */
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

char * check_chos(char *name)
{
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
