#define MODULE
#define __KERNEL__
#define __SMP__

/* some constants used in our module */
#define MODULE_NAME "chos"
#define MODULE_VERSION "0.02"

/* chos, Linux Kernel Module.
 * Verions: 0.01
 *
 * September 15, 2003
 *
 *    Copyright (C) 2003
 *    Copyright 2003 Regents of the University of California
 *             All rights reserved.
 *    Author: Shane Canon
 *
 * 
 *  Thanks to Frederic Dreier and Thomas Zimmermann
 *  for the examples given in their LLKM tutorial.
 
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
 *         -- 
 *
 */


#include <linux/kernel.h>
#include <linux/module.h> /* contains module declaration */
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/version.h>
#include <linux/proc_fs.h> /* contains all procfs methods signature */
#include <linux/list.h>
#include <linux/vmalloc.h>
#include <asm/unistd.h>

#include "chos.h"

EXPORT_NO_SYMBOLS;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,9)
#ifdef MODULE_LICENSE
MODULE_LICENSE("GPL");
#endif
#endif

long table=0;
int debug=0;
MODULE_PARM(debug, "i");
MODULE_PARM(table, "l");
MODULE_PARM_DESC(debug, "debug level (1-2)");
MODULE_PARM_DESC(table, "Memory location of table from save state");


#define DEFAULT "/"
#define MAXPATH 1024

/* variables */
struct chos *ch;
int save_state=0;


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
	strncpy(text,buffer,count);
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

void destroy_link(struct chos_link * link)
{
	if (link!=NULL && atomic_read(&(link->count))==0){/* Since no-one has a reference to it, we can delete it */
//	  printk("Destroying link to %s\n",link->text);
	  kfree(link->text);
	  kfree(link);
	}
}
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

void set_link(struct chos_link *link, struct task_struct *t)
{
	int i=0;
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
	p->start_time=t->start_time;
	write_unlock(&(p->lock));
}

void cleanup_links()
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
//  printk("CHOS: %d links purged\n",count);
}


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
    link=lookup_link(t->p_opptr);  /* Lookup parent */
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


/*
 * function: write_setchos
 *
 * */

int write_setchos(struct file* file, const char* buffer, unsigned long count, void* data)
{
	struct chos_link *link;

	if (current->euid!=0){
	  return -EPERM;
	}
	MOD_INC_USE_COUNT;
	cleanup_links();
	link=create_link(buffer,count);
	set_link(link,current);
	MOD_DEC_USE_COUNT;
	return count; 
}

int write_resetchos(struct file* file, const char* buffer, unsigned long count, void* data)
{
	MOD_INC_USE_COUNT;
	reset_link(&(ch->procs[current->pid]));
	cleanup_links();
	MOD_DEC_USE_COUNT;
	return count; 
}
int write_savestate(struct file* file, const char* buffer, unsigned long count, void* data)
{
	if (count>1){
	  if (buffer[0]=='1'){
	    printk("chos save state enabled\n");
	    printk("chos save state address = 0x%lx\n",ch);
	    save_state=1;
	  }
	  else{
	    printk("chos save state disabled\n");
	    save_state=0;
	  }
        }
}

int read_version(char* page, char** start, off_t off, int count, int* eof, void* data)
{
        int len;

        MOD_INC_USE_COUNT;
        len = sprintf(page, "%s %s %d %lu\n",MODULE_NAME,MODULE_VERSION,current->pid,current->start_time);
        *eof = 1;
        MOD_DEC_USE_COUNT;
        return len; /* return number of bytes returned */
}

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

static struct inode_operations link_inode_operations = {
        readlink:       link_readlink,
        follow_link:    link_follow_link
};


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
}

/*
 * function: init_module
 *
 * The main setup function.
 *
 */
int init_module(void)
{
	unsigned long i;
	unsigned long value;
	unsigned long *ptr;
        struct list_head *p;
        struct task_struct *t;
	struct proc_dir_entry *dir; /* pseudo file entries */

	struct proc_dir_entry *setchosfile; /* pseudo file entries */
	struct proc_dir_entry *resetchosfile; /* pseudo file entries */
	struct proc_dir_entry *linkfile; /* pseudo file entries */
	struct proc_dir_entry *versfile; /* pseudo file entries */
	struct proc_dir_entry *savestatefile; /* pseudo file entries */

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
//	dir->owner = THIS_MODULE;
	ch->dir = dir;

	/* make file */
	setchosfile = create_proc_entry("setchos", 0666, dir);
	if (setchosfile == NULL) goto fail_entry;
	/* set callback functions on file */
	setchosfile->write_proc = write_setchos;

	/* make file */
	resetchosfile = create_proc_entry("resetchos", 0666, dir);
	if (resetchosfile == NULL) goto fail_entry;
	/* reset callback functions on file */
	resetchosfile->write_proc = write_resetchos;

	/* make file */
	savestatefile = create_proc_entry("savestate", 0666, dir);
	if (savestatefile == NULL) goto fail_entry;
	/* save state callback functions on file */
	savestatefile->write_proc = write_savestate;

	/* make file */
	versfile = create_proc_entry("version", 0444, dir);
	if (versfile == NULL) goto fail_entry;
	/* reset callback functions on file */
	versfile->read_proc = read_version;

	linkfile = proc_symlink("link", dir, "/");
	if (linkfile == NULL) goto fail_entry;
	/* set callback functions on file */

	linkfile->proc_iops=&link_inode_operations;

	/* small output */
	printk ("%s %s module initialized..\n",MODULE_NAME,MODULE_VERSION);
	return 0;

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

	// remove setdn file entry
	remove_proc_entry("setchos", ch->dir);
	remove_proc_entry("resetchos", ch->dir);
	remove_proc_entry("version", ch->dir);
	remove_proc_entry("savestate", ch->dir);
	remove_proc_entry("link", ch->dir);
	remove_proc_entry(MODULE_NAME,NULL);
	if (!save_state){
	  vfree(ch->procs);
	  kfree(ch);
	}
	else{
          printk("State saved at 0x%lx\n",ch);
        }
	// print a small message
	printk("Module cleanup. Chos entry removed.\n");
}
