#include <stdio.h>
#include <stdlib.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>

#include "chos.h"
#include "config.h"

int set_multi();
char * check_chos(char *name);
char ** set_env();

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
   if (os==NULL){
     fprintf(stderr,"Error: requested os not allowed or error in check\n");
     return -2;
   }

   pw=getpwuid(getuid());
   if (pw==NULL){
     fprintf(stderr,"getpwuid failed for %d\n",getuid());
     return -2;
   }

   if (get_multi(chos)!=0){
     fprintf(stderr,"Failed to get chroot OS system link.\nPerhaps the system isn't fully configured.\n");
     return -3;
   }

/* If the current chos is different from the requested chos then update it */
   if (geteuid()!=0){
     fprintf(stderr,"Warning: Insufficient privelege to change environment (euid=%d).  Confirm that CHOS was successful\n",geteuid());
   }
   if (strncmp(chos,os,MAX_OS)!=0 && set_multi(os)!=0){
     fprintf(stderr,"Failed to set chroot OS system link.\nPerhaps there is a permission problem.\n");
     return -3;
   }

   if (!is_chrooted(chos) && chroot(CHROOT)!=0 ){
    fprintf(stderr,"chroot failed.\n");
    fprintf(stderr,"Perhaps this node isn't configured.\n");
    return -4;
  }

/* We now fork, so we can do some cleanup on exit.
 */

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

int set_multi(char *os)
{
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


int get_multi(char *os)
{
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


char ** set_env()
{
  FILE *stream;
  char **env;
  char *value;
  char *var;
  char buffer[80];
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
  buffer[0]=0;
  while(ret!=EOF){
    if (start){
      count++;
    }
    if (strcmp(buffer,ENVHEAD)==0){
      start=1;
    }
    ret=fscanf(stream,"%s",buffer);
  }
  env=(char **)malloc(sizeof(char *)*count);
  if (env==NULL){
    fprintf(stderr,"Failed to allocate memory for env\n");
    return NULL;
  }
  buffer[0]=0;
  start=0;
  ret=0;
  rewind(stream);
  while(ret!=EOF && setc<count){
    if (start){
      value=getenv(buffer);
      if (strcmp(buffer,"PATH")==0){
        env[setc]=DEFPATH;
        setc++;
      }
      else if (value!=NULL){
        len=strlen(buffer)+strlen(value)+2;
        var=(char *)malloc(len);
        if (var==NULL){
          fprintf(stderr,"failed to allocate memory\n");
          return;
        }
//        printf("%s=%s\n",buffer,value);
        sprintf(var,"%s=%s",buffer,value);
        env[setc]=var;
        setc++;
      }
//      else{
//        fprintf(stderr,"var %s not set\n",buffer);
//      }
    }
    if (strcmp(buffer,ENVHEAD)==0){
      start=1;
    }
    ret=fscanf(stream,"%s",buffer);
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
  static char buffer[80];
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

  buffer[0]=0;
  start=0;
  ret=0;
  while(ret!=EOF && retpath==NULL){
    if (start){
      if (buffer[0]=='%' || buffer[0]=='\n')
	break;
      path=strchr(buffer,':');
      *path=0;
      path++;
      if (strcmp(buffer,name)==0){
        retpath=path;
	break;
      }
    }
    if (strcmp(buffer,SHELLHEAD)==0){
      start=1;
    }
    ret=fscanf(cfile,"%s",buffer);
  }
  fclose(cfile);
  return retpath;
}
