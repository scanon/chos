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
 * job_starter.c
 *
 * Written by
 * Cary Whitney and Shane Canon
 *
 * This is a job starter for use with LSF and SGE.
 * Currently it does the following...
 *  - sets the memory size limit
 *    to the values defined in LIMIT_MB.  This
 *    should be in MB.
 *  - Limits a job to LIMIT_PROC sub processes
 *  - Looks for CHOS variable
 *  - Can log runs to LOG file
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>

// Limit memory to 1G
//#define LIMIT_MB 1024

// Limit number of processes
//#define LIMIT_PROC              50

// CHOS variable is CHOS
//#define CHOS "CHOS"

// Log jobs in job_starter.log
//#define LOG "/var/log/job_starter.log"


#define MB_FACTOR               1048580

int main(int argc, char *argv[]) {
        char **args;
        struct rlimit rlim;
        int ret;
	int i;
#ifdef CHOS
	char *chos;
#endif
#ifdef LOG
	FILE *log;
#endif

	if (argc==1){
		return -1;
	}

#ifdef LIMIT_MB
        rlim.rlim_cur = LIMIT_MB*MB_FACTOR;
        rlim.rlim_max = LIMIT_MB*MB_FACTOR;
        if(setrlimit(RLIMIT_AS, &rlim) != 0) {
                printf("resource failed to set\n");
        }
#endif

#ifdef LIMIT_PROC
        rlim.rlim_cur = LIMIT_PROC;
        rlim.rlim_max = LIMIT_PROC;
        if(setrlimit(RLIMIT_NPROC, &rlim) != 0) {
                printf("resource failed to set\n");
        }
#endif

#ifdef LOG
	log=fopen(LOG,"a");
	if (log!=NULL){
		for (i=0;i<argc;i++){
			fprintf(log,"%s ",argv[i]);
		}
		fprintf(log,"\n");
		fclose(log);
	}
#endif

#ifdef CHOS
	chos=getenv(CHOS);
	if (chos!=NULL && chos[0]!=0){
		args=(char **)malloc((argc+1)*sizeof(char *));
		for (i=1;i<argc;i++){
			args[i]=argv[i];
		}
		args[i]=NULL;
		args[0]="/usr/bin/chos";
	}
	else{
        	args = &argv[1];
	}
#else
        args = &argv[1];
#endif 

        execvp(args[0], args);
}

