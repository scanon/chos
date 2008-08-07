#define MODULE
#define __KERNEL__
#define __SMP__

/* some constants used in our module */
#define MODULE_NAME "chos"
#define MY_MODULE_VERSION "0.03"

/*
 * chos, Linux Kernel Module.
 *
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
 *  History
 *  -------
 *  0.01 - Initial release.
 *  0.02 - Significant changes
 *         -- Changed many arrays to struct
 *         -- Changed recover method to use struct
 *         -- Added auto cleanjp
 *         -- Added cacheing
 *         -- Use start_time to confirm validity of cache
 *         -- Don't allow write if not euid==0
 *  0.03 - Support for 2.6
 *
 */


#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/version.h>
#include <linux/proc_fs.h> /* contains all procfs methods signature */
#include <linux/list.h>
#include <linux/vmalloc.h>
#include <asm/unistd.h>
#include <asm/uaccess.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,99)
#include <linux/moduleparam.h>
#endif

#include "chos.h"

EXPORT_NO_SYMBOLS;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,9)
#ifdef MODULE_LICENSE
MODULE_LICENSE("BSD");
#endif
#endif

/* The table variable is used to pass in an address for a save state.
 * This can be used to save the process state across module upgrades.
 * Consider this feature experimental.
 */
long table=0;
int debug=0;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,99)
#define MOD_INC   /* nop */
#define MOD_DEC   /* nop */
module_param(debug, int,0);
module_param(table, long,0);
#define PID_MAX PID_MAX_DEFAULT
#else
#define MOD_INC  MOD_INC_USE_COUNT
#define MOD_DEC  MOD_DEC_USE_COUNT
MODULE_PARM(debug, "i");
MODULE_PARM(table, "l");
MODULE_PARM_DESC(debug, "debug level (1-2)");
MODULE_PARM_DESC(table, "Memory location of table from save state");
#endif

#ifdef MODULE_VERSION
MODULE_VERSION(MY_MODULE_VERSION);
#endif

#define DEFAULT "/"
#define MAXPATH 1024

/* variables */
struct chos *ch;
int save_state=0;

/*
 * This allocates and fills the chos_link structure.  This is typically
 * called when setlink is written to.
 */

struct chos_link * create_link(const char *buffer, unsigned long count)
{
	struct chos_link *link;
	char *text;
	char *p;
	int i=0;

	link=(struct chos_link *)kmalloc(sizeof(struct chos_link),GFP_KERNEL);
	if (link==NULL){
	  return NULL;
	}
	text=(char *)kmalloc(count+1,GFP_KERNEL);
        if (text==NULL)return NULL;
        if (__copy_from_user(text, buffer, count))
           return NULL;
        p=text;
	while(i<count && *p!='\n'){    /* Let's limit things to one line*/
	  i++;
          p++;
	}
	*p=0;
	link->text=text;
	atomic_set(&(link->count),0);
	return link;
}

/* 
 * This frees an instance of the chos_link structure.  The resetlink file
 * and cleanup can trigger this.
 */
void destroy_link(struct chos_link * link)
{
	if (link!=NULL && atomic_read(&(link->count))==0){/* Since no-one has a reference to it, we can delete it */
	  kfree(link->text);
	  kfree(link);
	}
}

/*
 * This removes the reference for a specific process.  If all the references
 * are removed for a link, the destroy link function is called.
 */
void reset_link(struct chos_proc *p)
{
	struct chos_link *link;

	write_lock(&(p->lock));
	link=p->link;
	p->link=NULL;
	p->start_time=0;
	write_unlock(&(p->lock));
	if (link!=NULL){
	  atomic_dec(&(link->count));
	  if (atomic_read(&(link->count))==0){
	    destroy_link(link);
	  }
	}
}

/*
 * This sets the reference for a specific process.
 */
void set_link(struct chos_link *link, struct task_struct *t)
{
	struct chos_proc *p;

	p=&(ch->procs[t->pid]);

	read_lock(&(p->lock));
	if (p->link!=NULL){
	  read_unlock(&(p->lock));
	  reset_link(p);
	}
	else{
	  read_unlock(&(p->lock));
	}

	atomic_inc(&(link->count));
	write_lock(&(p->lock));
	p->link=link;
/* The start time is used to determine if this structure is really
 * for this process.  For example, if the pids rolled over, this could
 * be for a previous process with the same pid.  The start time would
 * be different though.
 */
	p->start_time=t->start_time;
	write_unlock(&(p->lock));
}

/* 
 * This gets called periodically to remove stale "cache" entries
 */
void cleanup_links(void)
{
  int count=0;
  int i;
  struct task_struct *t;

  for (i=0;i<ch->pid_max;i++){
    t=find_task_by_pid(i);
    if (ch->procs[i].link!=NULL && t==NULL){
      reset_link(&(ch->procs[i]));
      count++;
    }
    else if (ch->procs[i].link!=NULL && ch->procs[i].start_time!=t->start_time){
      reset_link(&(ch->procs[i]));
      count++;
    }
  }
}


/* Whenever /proc/chos/link needs to be resolved, this function is called.
 * It will check if there is a valid cache by comparing the start times.
 * If the link isn't set or the start times disagree, we walk up the
 * process tree looking for a valid link.  If we get to process 0 or 1,
 * we return NULL.
 */
struct chos_link * lookup_link(struct task_struct *t)
{
  struct chos_link *link;
  struct chos_proc *p;

  if (t->pid==0 || t->pid==1){
    return NULL;
  }

  p=&(ch->procs[t->pid]);
  read_lock(&(p->lock));
  if (p->link==NULL || p->start_time!=t->start_time){  /* Not cached or incorrect */
    read_unlock(&(p->lock));
    link=lookup_link(t->PARENT);  /* Lookup parent */
    if (link!=NULL)
      set_link(link,t);
    else
      reset_link(p);
  }
  else{
    link=p->link;
    read_unlock(&(p->lock));
  }
  return link;
    
}


/* Here are the function handlers for the proc entries
 */


/*
 * This is called when something is written to /proc/chos/setchos.  It sets the
 * link for the calling process.
 */

int write_setchos(struct file* file, const char* buffer, unsigned long count, void* data)
{
	struct chos_link *link;

	if (current->euid!=0){
	  return -EPERM;
	}
	MOD_INC;
	cleanup_links();
	link=create_link(buffer,count);
	set_link(link,current);
	MOD_DEC;
	return count; 
}

/*
 * This is called when something is written to /proc/chos/resetchos.  It resets the link
 * for the calling process.
 */
int write_resetchos(struct file* file, const char* buffer, unsigned long count, void* data)
{
	MOD_INC;
	reset_link(&(ch->procs[current->pid]));
	cleanup_links();
	MOD_DEC;
	return count; 
}

/*
 * This is called when something is written to /proc/chos/savestate.  It changes the save
 * state flag.  This is only used for a module upgrade.
 */
int write_savestate(struct file* file, const char* buffer, unsigned long count, void* data)
{
	if (count>1){
	  if (buffer[0]=='1'){
	    printk("chos save state enabled\n");
	    printk("chos save state address = 0x%lx\n",(long)ch);
	    save_state=1;
	  }
	  else{
	    printk("chos save state disabled\n");
	    save_state=0;
	  }
        }
	return count;
}

/* Thie is called when /proc/chos/version is read.  It returns the version of chos.
 */
int read_version(char* page, char** start, off_t off, int count, int* eof, void* data)
{
        int len;

        MOD_INC;
        len = sprintf(page, "%s %s %d %lu\n",MODULE_NAME,MY_MODULE_VERSION,current->pid,current->start_time);
        *eof = 1;
        MOD_DEC;
        return len; /* return number of bytes returned */
}

/* This one and the next are the important ones.  This is the link that is used to point
 * to the different OS trees.  It calls lookup_link to resolve the link target.
 */
static int link_readlink(struct dentry *dentry, char *buffer, int buflen)
{
	struct chos_link *link;
	char *text;
	int ret;

	link=lookup_link(current);
	if (link==NULL)
	  text=DEFAULT;
	else
	  text=link->text;
	ret=vfs_readlink(dentry,buffer,buflen,text);

        return ret;
}

/* This is the link that is used to point to the different OS trees.  
 * It calls lookup_link to resolve the link target.
 */
static int link_follow_link(struct dentry *dentry, struct nameidata *nd)
{
	struct chos_link *link;
	char *text;
	int ret;

	link=lookup_link(current);
	if (link==NULL)
	  text=DEFAULT;
	else
	  text=link->text;
	ret=vfs_follow_link(nd,text);
 	return ret;
}

/*
 * This is the inode operations structure.  We will override the defaults for
 * our special link.
 */
static struct inode_operations link_inode_operations = {
        readlink:       link_readlink,
        follow_link:    link_follow_link
};


/*
 * chos init:
 * - Malloc the global struct
 * - initialize key variables (pid_max, magic, version)
 * - Malloc array for processes
 * - Initialize array
 */

int init_chos(void)
{
	int i;
	struct chos_proc *procs;
	size_t psize;

	ch=(struct chos *)kmalloc(sizeof(struct chos),GFP_KERNEL);
	if (ch==NULL){
	  printk("Unable to allocate chos handle\n");
	  return -1;
	}
	ch->pid_max=PID_MAX;
	psize=((size_t)(ch->pid_max))*sizeof(struct chos_proc);
	procs=(struct chos_proc *)vmalloc(psize);
	if (procs==NULL){
	  kfree(ch);
	  return -1;
	}
	ch->procs=procs;
	for (i=0;i<ch->pid_max;i++){
	  ch->procs[i].link=NULL;
	  ch->procs[i].start_time=-1;
	  rwlock_init(&(ch->procs[i].lock));
	}
	ch->magic=0x1234;
	ch->version=1;
	return 0;
}

/*
 * Restore global chos struct.  Confirm that version
 * and magic number are correct.
 */
int recover_chos(long table)
{
	struct chos *ptr=(struct chos *)table;

	if (ptr->magic!=0x1234){
	  return -1;
	}
	if (ptr->version==CDSV){
	  ch=ptr;
	  return 0;
	}
	return -1;
}

/*
 * function: init_module
 *
 * The main setup function.
 * - Create the proc entries
 * - If table is passed, then try to recover.
 * - Otherwise call init_chos
 *
 */
int init_module(void)
{
	struct proc_dir_entry *dir;
	struct proc_dir_entry *setchosfile;
	struct proc_dir_entry *resetchosfile;
	struct proc_dir_entry *linkfile;
	struct proc_dir_entry *versfile;
	struct proc_dir_entry *savestatefile;

	if (table!=0){
	  printk("Recoverying table from 0x%lx\n",table);
	  if (recover_chos(table)!=0){
            printk("Unable to recover.  Bad magic\n");
	    return -1;
	  }
	  else{
	    printk("Recovery successful.\n");
	  }

        }
	else{
	  if (init_chos()!=0){
	    printk("<0>ERROR allocating tables\n");
  	    return -1;
	  }
	}
	/* make a directory in /proc */
	dir= proc_mkdir(MODULE_NAME, NULL);
	if (dir == NULL) goto fail_dir;
/*	There seems to be a problem in the proc code that
 *      causes the module use count to not get decremented.
 *      I think this is fixed in 2.6.
 */ 
/*	dir->owner = THIS_MODULE; */
	ch->dir = dir;

/* This is the file used to set the value (target) of the link. */
	setchosfile = create_proc_entry("setchos", 0666, dir);
	if (setchosfile == NULL) goto fail_entry;
	setchosfile->write_proc = write_setchos;

/* This is the file that is used to reset the link. */
	resetchosfile = create_proc_entry("resetchos", 0666, dir);
	if (resetchosfile == NULL) goto fail_entry;
	resetchosfile->write_proc = write_resetchos;

/* This enables the save state flag. */
	savestatefile = create_proc_entry("savestate", 0666, dir);
	if (savestatefile == NULL) goto fail_entry;
	savestatefile->write_proc = write_savestate;

/* This is used to read the version. */
	versfile = create_proc_entry("version", 0444, dir);
	if (versfile == NULL) goto fail_entry;
	versfile->read_proc = read_version;

/* This is the all important specia link. */
	linkfile = proc_symlink("link", dir, "/");
	if (linkfile == NULL) goto fail_entry;

/* Set the operations struct to our custom version */
	linkfile->proc_iops=&link_inode_operations;

	/* Report success */
	printk ("%s %s module initialized..\n",MODULE_NAME,MY_MODULE_VERSION);
	return 0;

/* This is incomplete at this time */
fail_entry:
	printk("<1>ERROR creating setchos\n");
	remove_proc_entry(MODULE_NAME,NULL);
fail_dir:
	printk("<1>ERROR creating chos directory\n");
	return -1;
}

/*
 * Cleanup module
 */
void cleanup_module(void)
{

	/* remove chos proc file entries */
	remove_proc_entry("setchos", ch->dir);
	remove_proc_entry("resetchos", ch->dir);
	remove_proc_entry("version", ch->dir);
	remove_proc_entry("savestate", ch->dir);
	remove_proc_entry("link", ch->dir);
	remove_proc_entry(MODULE_NAME,NULL);
/*
 * If save state isn't set, free up global struct and array.  Otherwise,
 * leave them around.
 */
	if (!save_state){
	  vfree(ch->procs);
	  kfree(ch);
	}
	else{
          printk("State saved at 0x%lx\n",(long)ch);
        }
	printk("Module cleanup. Chos entry removed.\n");
}
