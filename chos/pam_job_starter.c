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
 * pam_job_starter.c
 *
 * Written by
 * Shane Canon <canon@nersc.gov>
 *
 * This is a pam enabled job starter
 * Currently it does the following...
 *  - sets the memory size limit
 *    to the values defined in LIMIT_MB.  This
 *    should be in MB.
 *  - Limits a job to LIMIT_PROC sub processes
 *  - Looks for CHOS variable
 *  - Can log runs to LOG file
 *
 */
#include <security/pam_appl.h>
#include <security/pam_misc.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pwd.h>
#include <sys/types.h>

       struct passwd *getpwnam(const char *name);





static struct pam_conv conv = {
    misc_conv,
    NULL
};

// Limit memory to 1G
//#define LIMIT_MB 1024

// Limit number of processes
//#define LIMIT_PROC              50

// CHOS variable is CHOS
//#define CHOS "CHOS"

// Log jobs in job_starter.log
//#define LOG "/var/log/job_starter.log"

#define PAM_NAME	"jobstarter"


#define MB_FACTOR               1048580

int main(int argc, char *argv[]) {
	char **args;
	pam_handle_t *pamh=NULL;
	int retval;
	struct passwd *pw;


	if (argc==1){
		return -1;
	}
	pw=getpwuid(getuid());


	retval = pam_start(PAM_NAME, pw->pw_name, &conv, &pamh);

	if (retval == PAM_SUCCESS)
		retval = pam_acct_mgmt(pamh, 0);	/* permitted access? */
	else
		fprintf(stderr,"%s: pam_start failed\n",PAM_NAME);
	
	if (retval == PAM_SUCCESS)
		retval = pam_open_session(pamh, 0);
	else
		fprintf(stderr,"%s: pam_acct failed\n",PAM_NAME);
		
	if (retval != PAM_SUCCESS)
		fprintf(stderr,"%s: pam_acct failed\n",PAM_NAME);
	

	if (pam_end(pamh,retval) != PAM_SUCCESS) {	/* close Linux-PAM */
		pamh = NULL;
		fprintf(stderr, "check_user: failed to release authenticator\n");
		exit(1);
	}

	if (retval != PAM_SUCCESS)
		return 99;

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

	args = &argv[1];

	execvp(args[0], args);
}
