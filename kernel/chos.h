
// Version 1 chos data structures

#define CDSV 1

struct chos_link {
	char *text;
	atomic_t count;
};

struct chos_proc {
	struct chos_link *link;
	unsigned long start_time;
	rwlock_t lock;
};


struct chos {
	int magic;
	int version;
	int pid_max;
	struct proc_dir_entry *dir; /* pseudo file entries */
	struct chos_proc *procs;
};
