/*
 * Author: Shane Canon <canon@nersc.gov>
 *
 * chos (c) 2004, The Regents of the University of California, through
 *   Lawrence Berkeley National Laboratory (subject to receipt of any required
 *   approvals from the U.S. Dept. of Energy).  All rights reserved.
 *
 * If you have questions about your rights to use or distribute this software,
 * please contact Berkeley Lab's Technology Transfer Department at  TTD@lbl.gov
 * referring to "CHOS (LBNL Ref CR-2025)"
 *
 * NOTICE.  This software was developed under funding from the U.S. Department
 * of Energy.  As such, the U.S. Government has been granted for itself and
 * others acting on its behalf a paid-up, nonexclusive, irrevocable, worldwide
 * license in the Software to reproduce, prepare derivative works, and perform
 * publicly and display publicly.  Beginning five (5) years after the date
 * permission to assert copyright is obtained from the U.S. Department of
 * Energy, and subject to any subsequent five (5) year renewals, the U.S.
 * Government is granted for itself and others acting on its behalf a paid-up,
 * nonexclusive, irrevocable, worldwide license in the Software to reproduce,
 * prepare derivative works, distribute copies to the public, perform publicly
 * and display publicly, and to permit others to do so.
 *
 *
 * Description:
 *
 *
 */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,99)
#define KERN26
#define PARENT parent
#else
#define PARENT p_opptr
#endif

// Version 1 chos data structures

#define CDSV 1

/*
 * This is the text for the symbolic link.  When something is echoed
 * into setlink, an instance of this struct is allocated and filled.
 * All children link to the parent instance and the ref count is
 * incremented.
 */
struct chos_link {
	char *text;
	atomic_t count;
};

/*
 * An array of this structure with max_pid entries is created.
 * The link is cached for each process.  The start_time is compared
 * to the pid start_time to see if the cache entry is valid.
 */
struct chos_proc {
	struct chos_link *link;
#ifdef KERN26
       u64 start_time;
#else
        unsigned long start_time;
#endif
	rwlock_t lock;
};

/*
 * This is the global chos struct.  It contains the global
 * config info.  During a "save state" this structure isn't
 * freed, but is instead given to the new module on load.
 */
struct chos {
	int magic;
	int version;
	int pid_max;
	struct proc_dir_entry *dir; /* pseudo file entries */
	struct chos_proc *procs;
};

