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

#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../chos.h"
#include "../config.h"

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

   if(!(configure_chos())) {
     fprintf(stderr,"Failed to initialize CHOS.\n");
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
   if (os && strncmp(chos,os,MAX_OS)!=0 && set_multi(osenv)!=0){
     fprintf(stderr,"Failed to set CHOS.\nPerhaps there is a permission problem.\n");
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

