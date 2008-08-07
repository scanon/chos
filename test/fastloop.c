#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/wait.h>

int main(int argc, char *argv[])
{
   pid_t pid;
   int count=0;
   char path[80];
   int len;
   int max;

   if (argc<2){
     fprintf(stderr,"%s <loops>\n",argv[0]);
     return -1;
   }

   max=atoi(argv[1]);
   while (count<max){
   if (count%100==0){
     fprintf(stderr,"loop %d\n",count);
   }
   pid=fork();
   if (pid==0){  // Child
       len=readlink("/proc/chos/link",path,80);
       path[len]=0;
       if (strcmp(path,getenv("CHOS"))!=0){
         printf("Different %s %s %d\n",path,getenv("CHOS"),getpid());
       }
       return 0;
   }
   else{
     waitpid(pid,NULL,0);
   }
   count++;
   }
}

