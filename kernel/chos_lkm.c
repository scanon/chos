#ifndef MODULE
#define MODULE
#define __KERNEL__
#define __SMP__
#endif

/* some constants used in our module */
#define MODULE_NAME "chos"
#define MY_MODULE_VERSION "0.13.0"

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
 *  0.01   - Initial release.
 *  0.02   - Significant changes
 *           -- Changed many arrays to struct
 *           -- Changed recover method to use struct
 *           -- Added auto cleanjp
 *           -- Added cacheing
 *           -- Use start_time to confirm validity of cache
 *           -- Don't allow write if not euid==0
 *  0.03   - Support for 2.6
 *  0.08   - Fix for follow_link to use ERR_PTR macro
 *  0.09   - Remove exception for root in setting the link
 *  0.10   - Port to el6 kernel family
 *  0.11   - Add feature to allow exiting CHOS from within CHOS
 *  0.11.1 - Fix to allow re-entering prior CHOS environment after
 *               exiting CHOS
 *  0.11.2 - Explicitly set new processes' CHOS links after do_fork()
 *               returns to handle some scenarios where child
 *               processes quickly fork new children and then exit
 *  0.12.0 - Improve several log messages
 *  0.12.1 - Improve handling of short-lived processes in
 *           chos_do_fork()
 *  0.13.0 - Add support for el7 kernel family
 *
 */


#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/version.h>
#include <linux/err.h>
#include <linux/list.h>
#include <linux/proc_fs.h> /* contains all procfs methods signature */
#include <linux/vmalloc.h>
#include <asm/unistd.h>
#include <asm/uaccess.h>
#include <asm/cacheflush.h>
#include <asm/pgtable.h>
#include <linux/list.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,99)
#include <linux/fs_struct.h>
#include <linux/moduleparam.h>
#include <linux/namei.h>
#endif
#include <linux/sched.h>
#include <asm/desc.h>

#include "chos.h"
#include "address.h"


#ifdef PID_NS
#include <linux/nsproxy.h>
#endif

//EXPORT_NO_SYMBOLS;

#ifdef MODULE_LICENSE
MODULE_LICENSE("BSD");
#endif

/* The table variable is used to pass in an address for a save state.
 * This can be used to save the process state across module upgrades.
 * Consider this feature experimental.
 */
long table=0;
int debug=0;
long max=0;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,99)
module_param(debug, int,0);
module_param(table, long,0);
module_param(max, long,0);
#else
MODULE_PARM(debug, "i");
MODULE_PARM(table, "l");
MODULE_PARM(max, "l");
MODULE_PARM_DESC(debug, "debug level (1-2)");
MODULE_PARM_DESC(table, "Memory location of table from save state");
MODULE_PARM_DESC(max, "Size of process array");
#endif

#ifdef MODULE_VERSION
MODULE_VERSION(MY_MODULE_VERSION);
#endif

#define DEFAULT "/"
#define MAXPATH 1024

/* variables */
struct chos *ch;
int save_state=0;


#if defined(START_ADD) && defined(END_ADD)
#define WRAP_DOFORK
int init_do_fork(void);
void cleanup_do_fork(void);
#endif
#if defined(START_ADD) && defined(LENGTH)
#define WRAP_DOFORK
#define END_ADD (START_ADD+LENGTH)
#endif

#ifdef WRAP_DOFORK
int init_do_fork(void);
void cleanup_do_fork(void);
#endif

#ifdef SCT
int (*orig_sys_exit)(int error_code);
void (*orig_sys_exit_group)(int error_code);
long my_sys_exit(int error_code);
void my_sys_exit_group(int error_code);
#endif


/* declarations */
int is_valid_path(const char *path);
DO_FORK_RET chos_do_fork(unsigned long clone_flags,
            unsigned long stack_start,
            struct pt_regs *regs,
            unsigned long stack_size,
            TIDPTR_T *parent_tidptr,
            TIDPTR_T *child_tidptr);
DO_FORK_RET jumper(unsigned long clone_flags,
            unsigned long stack_start,
            struct pt_regs *regs,
            unsigned long stack_size,
            TIDPTR_T *parent_tidptr,
            TIDPTR_T *child_tidptr);

/*
 * This allocates and fills the chos_link structure.  This is typically
 * called when setlink is written to.
 */

struct chos_link * create_link(char *text)
{
  struct chos_link *link;

  link=(struct chos_link *)kmalloc(sizeof(struct chos_link),GFP_KERNEL);
  if (link==NULL){
    return NULL;
  }
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
  pid_t global_pid;

  BUG_ON(t == NULL);

#ifdef TASK_PID_NR
  global_pid = task_pid_nr(t);
#else
  global_pid = t->pid;
#endif

  BUG_ON(global_pid > ch->pid_max);

  p=&(ch->procs[global_pid]);

  read_lock(&(p->lock));
  if (p->link!=NULL){
    read_unlock(&(p->lock));
    reset_link(p);
  }
  else{
    read_unlock(&(p->lock));
  }

  if (link!=NULL){
    atomic_inc(&(link->count));
  }
  write_lock(&(p->lock));
  p->link=link;
  /* The start time is used to determine if this structure is really
   * for this process.  For example, if the pids rolled over, this could
   * be for a previous process with the same pid.  The start time would
   * be different though.
   */
  p->start_time=t->start_time.tv_sec;
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

  write_lock_irq(tasklist_lock_p);
  for (i=0;i<ch->pid_max;i++){
#ifdef PID_NS
    t=s_find_task_by_pid_ns(i,current->nsproxy->pid_ns);
#else
     t=find_task_by_pid(i);
#endif
    if (ch->procs[i].link!=NULL && t==NULL){
      reset_link(&(ch->procs[i]));
      count++;
    }
    else if (ch->procs[i].link!=NULL && ch->procs[i].start_time!=t->start_time.tv_sec){
      reset_link(&(ch->procs[i]));
      count++;
    }
  }
  write_unlock_irq(tasklist_lock_p);
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

  if (t==NULL){
    return NULL;
  }

  if (t->pid==0 || t->pid==1){
    return NULL;
  }

  BUG_ON(t->pid > ch->pid_max);

  p=&(ch->procs[t->pid]);
  read_lock(&(p->lock));
  if (p->link==NULL || p->start_time!=t->start_time.tv_sec){  /* Not cached or incorrect */
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

int is_chrooted(void)
{
  int retval=0;

//  if (strcmp(dentry->d_name.name,"chos")==0){
// Already chrooted

#ifdef STRUCT_PATH
/* Moved to struct path by 4ac9137858e08a19f29feac4e1f4df7c268b0ba5
 * http://lwn.net/Articles/206758/
 */
  if (ch->named.path.mnt==current->fs->root.mnt && ch->named.path.dentry->d_inode==current->fs->root.dentry->d_inode){
#else
  if (ch->named.mnt==current->fs->rootmnt && ch->named.dentry->d_inode==current->fs->root->d_inode){
#endif
    retval=1;
  }
  return retval;
}

int my_chroot(const char *path)
{
  KERNEL_CAP_T capback;
  int retval;
  mm_segment_t mem;

#ifdef USE_CRED
  capback=current->cred->cap_effective;
  /*
   * TODO: This code for changing cap_effective appears to work, but
   * looks too ugly to be the right way to do it 
   */
  cap_raise(*(KERNEL_CAP_T *)&(current->cred->cap_effective),CAP_SYS_CHROOT);
#else
  capback=current->cap_effective;
  cap_raise(current->cap_effective,CAP_SYS_CHROOT);
#endif
  mem=get_fs(); 
  set_fs(KERNEL_DS);
  if ((retval=sys_chroot(path))!=0){
    printk("%s: chroot failed\n",MODULE_NAME);
  }
#ifdef USE_CRED
  (*(KERNEL_CAP_T *)&current->cred->cap_effective)=capback;
#else
  current->cap_effective=capback;
#endif
  set_fs(mem);
  return retval;
}


/* Here are the function handlers for the proc entries
 */


/*
 * This is called when something is written to /proc/chos/setchos.  It sets the
 * link for the calling process.
 */
ssize_t write_setchos(struct file *filp, const char *buf, size_t count, loff_t *offp) {
  struct chos_link *link;
  char *text;
  int i=0;
  int retval;

  i=0;
  while (i<(count) && buf[i]!='\n' && buf[i]!=0){
     i++;
  }
  text=(char *)kmalloc(i,GFP_KERNEL);
  if (text){
     retval = __copy_from_user(text, buf, i);
     if(retval > 0) {
         printk("%s: __copy_from_user returned %d\n.",MODULE_NAME, retval);
     }
     text[i]=0;
  }
  else{
	  return -ENOMEM;
  }

  if (text[0]=='/' && text[1]==0 ){
#ifdef STRUCT_PATH
    set_fs_root_p(current->fs,&(ch->nochroot.path));
#else
    set_fs_root_p(current->fs,ch->nochroot.mnt,ch->nochroot.dentry); 
#endif
    cleanup_links();
    link=create_link(text);
    set_link(link,current);
    return count;
  }
 

  if (!is_valid_path(text)){
#if defined(USE_CRED) && defined(KUID_T_IS_STRUCT)
    printk("%s:Attempt to use invalid path. uid=%d (Requested %s)\n",
            MODULE_NAME,current->cred->uid.val,text);
#elif defined(USE_CRED)
    printk("%s:Attempt to use invalid path. uid=%d (Requested %s)\n",MODULE_NAME,current->cred->uid,text);
#else
    printk("%s: Attempt to use invalid path. uid=%d (Requested %s)\n",MODULE_NAME,current->uid,text);
#endif
    return -ENOENT;
  }
//  printk("%s: is_valid_path: %d\n",text,is_valid_path(text));
//  printk("uid: %d euid: %d suid:%d fsuid:%d\n",current->uid,current->euid,current->suid,current->fsuid);
  MOD_INC;
  cleanup_links();
  link=create_link(text);
  set_link(link,current);
  if (!is_chrooted()){
    my_chroot(CHOSROOT);
  }
  MOD_DEC;

  return count; 
}

/*
 * This is called when something is written to /proc/chos/resetchos.  It resets the link
 * for the calling process.
 */
ssize_t write_resetchos(struct file *filp, const char *buf, size_t count, loff_t *offp) {
  MOD_INC;
  BUG_ON(current->pid > ch->pid_max);

  reset_link(&(ch->procs[current->pid]));
  cleanup_links();
  MOD_DEC;
  return count; 
}

/*
 * This is called when something is written to /proc/chos/savestate.  It changes the save
 * state flag.  This is only used for a module upgrade.
 */
ssize_t write_savestate(struct file *filp, const char *buf, size_t count, loff_t *offp) {
  if (count>1){
    if (buf[0]=='1'){
      printk("%s: save state enabled\n",MODULE_NAME);
      printk("%s: save state address = 0x%lx\n",MODULE_NAME,(long)ch);
      save_state=1;
    }
    else{
      printk("%s: save state disabled\n",MODULE_NAME);
      save_state=0;
    }
  }
  return count;
}


/*
 * This is called when /proc/chos/version is opened.  It returns the
 * version of the CHOS kernel module.
 */
static int print_version(struct seq_file *m, void *v)
{
  char *yes_fork="Fork trapped";
  char *no_fork="Fork not trapped";
  char *text_fork;

  MOD_INC;
  if (ch->fork_wrapped){
    text_fork=yes_fork;
  }
  else{
    text_fork=no_fork;
  }
  seq_printf(m,"%s %s %s\n",MODULE_NAME,MY_MODULE_VERSION,text_fork);
  MOD_DEC;
  return 0; /* return number of bytes returned */
}

static int version_open(struct inode *inode, struct file *file) {
  return single_open(file, print_version, NULL);
}


void add_valid_path(char *path,int len)
{
  struct valid_path *new;

  new=(struct valid_path *)kmalloc(sizeof(struct valid_path),GFP_KERNEL);
  if (new){
    new->path=path;
    new->length=len;
    list_add_tail(&(new->list),&valid_paths);
  }
}

void remove_valid_path(struct valid_path *v)
{
  if (v!=NULL){
    list_del(&(v->list));
    kfree(v->path);
    kfree(v);
  }
}

void listvalid(void)
{
}

void resetvalid(void)
{
  struct valid_path *c;
  struct list_head *p;

  for (p=valid_paths.next;p!=&valid_paths;){
    c=list_entry(p,struct valid_path,list);
    p=p->next;
    remove_valid_path(c);
  }
}

ssize_t write_valid(struct file *filp, const char *buf, size_t count, loff_t *offp) {
  char *path;
  int i;
  int retval;

  MOD_INC;

#if defined(USE_CRED) && defined(KUID_T_IS_STRUCT)
  if (current->cred->euid.val!=0){
#elif defined(USE_CRED)
  if (current->cred->euid!=0){
#else
  if (current->euid!=0){
#endif
    return -EPERM;
  }
  if (buf[0]=='-'){
    printk("%s: Resetting\n",MODULE_NAME);
    resetvalid();
  }
  else{
   i=0;
   while (i<(count) && buf[i]!='\n' && buf[i]!=0)
     i++;
   path=(char *)kmalloc(i,GFP_KERNEL);
   if (path){
     path[i]=0;
     retval = __copy_from_user(path, buf, i);
     if(retval > 0) {
         printk("%s: __copy_from_user returned %d\n.",MODULE_NAME,retval);
     }
     add_valid_path(path,i);
   }
   else{
     return 0;
   }
  }
  MOD_DEC;
  return count;
}

/* Print a list of valid CHOS paths. */
static int print_valid(struct seq_file *m, void *v) {
  struct valid_path *c;
  struct list_head *p;

  for (p=valid_paths.next;p!=&valid_paths;){
    c=list_entry(p,struct valid_path,list);
    seq_printf(m, "%s\n", c->path);
    p=p->next;
  }
  return 0;
}

static int valid_open(struct inode *inode, struct file *file) {
  return single_open(file, print_valid, NULL);
}

int is_valid_path(const char *path)
{
  struct valid_path *c;
  struct list_head *p;
  for (p=valid_paths.next;p!=&valid_paths;){
    c=list_entry(p,struct valid_path,list);
    if (strcmp(path,c->path)==0)
      return 1;
    p=p->next;
  }
  return 0;
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

static void * link_follow_link(struct dentry *dentry, struct nameidata *nd)
{
  struct chos_link *link;
  char *text;
  void *ret;

  link=lookup_link(current);
  if (link==NULL)
    text=DEFAULT;
  else
    text=link->text;
  ret=ERR_PTR(vfs_follow_link(nd,text));
  return ret;
}

/*
 * This is the inode operations structure.  We will override the defaults for
 * our special link.
 */
static struct inode_operations link_inode_operations = {
        .readlink=       link_readlink,
        .follow_link=    link_follow_link,
};

/* Operations for /proc/chos/setchos */
static struct file_operations setchos_fops = {
        .write = write_setchos
};

/* Operations for /proc/chos/resetchos */
static struct file_operations resetchos_fops = {
        .write = write_resetchos
};

/* Operations for /proc/chos/savestate */
static struct file_operations savestate_fops = {
        .write = write_savestate
};

/* Operations for /proc/chos/version */
static struct file_operations version_fops = {
        .open       = version_open,
        .read       = seq_read,
        .llseek     = seq_lseek,
        .release    = single_release
};

/* Operations for /proc/chos/valid */
static struct file_operations valid_fops = {
        .write      = write_valid,
        .open       = valid_open,
        .read       = seq_read,
        .llseek     = seq_lseek,
        .release    = single_release
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
  int retval=0;

  ch=(struct chos *)kmalloc(sizeof(struct chos),GFP_KERNEL);
  if (ch==NULL){
    printk("MODULE_NAME: Unable to allocate chos handle\n");
    return -1;
  }

#if defined(HAS_LOOKUP_NOALT) && defined(HAS_PATH_LOOKUP)
  retval=path_lookup(CHOSROOT, LOOKUP_FOLLOW | LOOKUP_DIRECTORY | LOOKUP_NOALT,&(ch->named)); 
#elif defined(HAS_PATH_LOOKUP)
  /*
   * TODO: Find out *why* was LOOKUP_NOALT was removed in
   * 7f2da1e7d0330395e5e9e350b879b98a1ea495df and what the
   * implications are
   */
  retval=path_lookup(CHOSROOT, LOOKUP_FOLLOW | LOOKUP_DIRECTORY,&(ch->named)); 
  retval=path_lookup("/", LOOKUP_FOLLOW | LOOKUP_DIRECTORY,&(ch->nochroot)); 
#else
  retval=path_lookupat_p(AT_FDCWD,CHOSROOT, LOOKUP_FOLLOW | LOOKUP_DIRECTORY,&(ch->named)); 
  retval=path_lookupat_p(AT_FDCWD,"/", LOOKUP_FOLLOW | LOOKUP_DIRECTORY,&(ch->nochroot)); 
#endif
  
  if (max>0)
    ch->pid_max=max;
  else
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
  ch->fork_wrapped=0;

#ifdef WRAP_DOFORK  
  if (init_do_fork()==0){
    ch->fork_wrapped=1;
  }
#endif
#ifdef SCT
  orig_sys_exit=sys_call_table[__NR_exit];
  orig_sys_exit_group=sys_call_table[__NR_exit_group];
  printk("%s: sys_call_table=%lx\n",MODULE_NAME,(unsigned long)sys_call_table);
  printk("%s: orig exit=%lx\n",MODULE_NAME,(unsigned long)orig_sys_exit);
  printk("%s: new exit=%lx\n",MODULE_NAME,(unsigned long)my_sys_exit);
//  change_page_attr(virt_to_page(sys_call_table),1,PAGE_KERNEL);
//  global_flush_tlb();
  sys_call_table[__NR_exit]=my_sys_exit;
  sys_call_table[__NR_exit_group]=my_sys_exit_group;
#endif
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
 * function: handle_proc_entry_failure
 *
 * Attempt to recover from a failure to create a proc_dir_entry.
 */
static void handle_proc_entry_failure(void) {
    /* This is incomplete at this time */
    printk("%s: <1>ERROR creating setchos\n",MODULE_NAME);
    remove_proc_entry(MODULE_NAME,NULL);
}

/*
 * function: chos_make_proc_entry
 * Create a proc entry with the passed name, mode, parent directory,
 * and operations
 */
static void chos_make_proc_entry(char *name, umode_t mode,
        struct proc_dir_entry *parent, struct file_operations *fops) {

    struct proc_dir_entry *f;
#ifdef HAS_PROC_CREATE
    f = proc_create(name, mode, parent, fops);
#else
    f = create_proc_entry(name, mode, parent);
#endif

  if (f == NULL ) {
      handle_proc_entry_failure();
  }
#ifndef HAS_PROC_CREATE
  else {
    f->proc_fops = fops;
  }
#endif

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
  struct proc_dir_entry *linkfile;

  if (table!=0){
    printk("%s: Recoverying table from 0x%lx\n",MODULE_NAME, table);
    if (recover_chos(table)!=0){
      printk("%s: Unable to recover.  Bad magic\n",MODULE_NAME);
      return -1;
    }
    else{
      printk("%s: Recovery successful.\n",MODULE_NAME);
    }

  }
  else{
    if (init_chos()!=0){
      printk("%s: <0>ERROR allocating tables\n",MODULE_NAME);
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
  chos_make_proc_entry("setchos", 0666, dir, &setchos_fops);

  /* This is the file that is used to reset the link. */
  chos_make_proc_entry("resetchos", 0666, dir, &resetchos_fops);

  /* This enables the save state flag. */
  chos_make_proc_entry("savestate", 0600, dir, &savestate_fops);

  /* This is used to read the version. */
  chos_make_proc_entry("version", 0444, dir, &version_fops);

  /* This is used to read the version. */
  chos_make_proc_entry("valid", 0600, dir, &valid_fops);

  /* This is the all important special link. */
  linkfile = proc_symlink("link", dir, "/");
  if (linkfile == NULL) handle_proc_entry_failure();

  /* Set the inode operations struct to our custom version. */
#ifdef HAS_PROC_DIR_ENTRY_DEF
  linkfile->proc_iops=&link_inode_operations;
#else
  /* Set the linkfile's proc_iops to link_inode_operations without
   * using the definition of proc_dir_entry:
   * 1) Begin with the address of proc_iops within the linkfile
   *    structure (at position +0x20)
   * 2) Cast this address as an inode_operations **
   * 3) Change the value located at this address to
   *    the address of link_inode_operations.
   */
  *( (const struct inode_operations **) (((unsigned long)linkfile)+0x20) ) = &link_inode_operations;
#endif

  /* Report success */
  printk ("%s %s module initialized..\n",MODULE_NAME,MY_MODULE_VERSION);
  return 0;

fail_dir:
  printk("%s: <1>ERROR creating chos directory\n",MODULE_NAME);
  return -1;
}

/*
 * Cleanup module
 */
void cleanup_module(void)
{

#ifdef WRAP_DOFORK
  if (ch->fork_wrapped){
    cleanup_do_fork();
  }
#endif
#ifdef SCT
  sys_call_table[__NR_exit]=orig_sys_exit;
  sys_call_table[__NR_exit_group]=orig_sys_exit_group;
#endif
  /* remove chos proc file entries */
  remove_proc_entry("setchos", ch->dir);
  remove_proc_entry("resetchos", ch->dir);
  remove_proc_entry("version", ch->dir);
  remove_proc_entry("savestate", ch->dir);
  remove_proc_entry("link", ch->dir);
  remove_proc_entry("valid", ch->dir);
  remove_proc_entry(MODULE_NAME,NULL);
  /*
   * If save state isn't set, free up global struct and array.  Otherwise,
   * leave them around.
   */
  if (!save_state){
#ifdef PATH_PUT
    path_put(&(ch->named).path);
#else
    path_release(&(ch->named));
#endif
    vfree(ch->procs);
    kfree(ch);
  }
  else{
    printk("%s: State saved at 0x%lx\n",MODULE_NAME,(long)ch);
  }
  printk("%s: Module cleanup. Chos entry removed.\n",MODULE_NAME);
}

#ifdef WRAP_DOFORK  
int init_do_fork(void)
{
  unsigned char *ptr;
  unsigned char *newptr;
  unsigned char *start=(unsigned char *)START_ADD;
  unsigned char *end=(unsigned char *)END_ADD;
  long *lptr;
  long diff;
  int i;

  ptr=(unsigned char *)START_ADD;

#ifdef LENGTH
  for (i=0;i<LENGTH;i++,ptr++){
    if (*ptr!=opcode[i]){
      printk("MODULE_NAME: Detected code mismatch when attempting to trap do_fork: %x %x\n",*ptr,opcode[i]);
      return -1;
    }
  }
#endif

  ptr=(unsigned char *)START_ADD;

  /* Copy instructions to jumper */
  newptr=(unsigned char *)(jumper);
//  printk("Copying function to jumper 0x%llx\n", newptr);
  for (ptr=start;ptr<end;ptr++,newptr++){
    *newptr=*ptr;
  }

  /* Modify jumper to jump back to original function at
     next instruction boundary */
//  printk("Adding jump in jumper 0x%llx\n", newptr);
  *newptr=0xe9;  /* jump */
  newptr++;
  lptr=(long *)(newptr);
  newptr+=4;  /* address of the next instruction*/
  diff=((void *)END_ADD-(void *)newptr);
//  printk(" diff %d\n",diff);
  if (diff<0xffffff){
    *lptr=diff;
  }
  else{
    return -1;
  }

  /* Now modify the original function to jump to the
     wrapper */
  ptr=(unsigned char*)START_ADD;
//  printk("Adding jump in original function 0x%llx\n", ptr);
  *ptr=0xe9;  /* jump */
  ptr++;
  lptr=(long *)(ptr);
  ptr+=4;  /* address of the next instruction*/
  diff=((void *)(chos_do_fork)-(void *)ptr);
//  printk(" diff %d\n",diff);
  *lptr=diff;


//  printk ("Fix brk installed..\n");           /* All done. */
  return 0;                                   /* success */

}


void cleanup_do_fork(void)
{
  unsigned char *ptr;
  unsigned char *newptr;
  unsigned char *start=(unsigned char *)START_ADD;
  unsigned char *end=(unsigned char *)END_ADD;

  ptr=(unsigned char *)START_ADD;

  /* Copy instructions to jumper */
  newptr=(unsigned char *)(jumper);
  for (ptr=start;ptr<end;ptr++,newptr++){
    *ptr=*newptr;
  }
}

/* These are the first couple of lines from the patched mmap.c */
/* Do the new checks and then call the jumper function         */

DO_FORK_RET chos_do_fork(unsigned long clone_flags,
            unsigned long stack_start,
            struct pt_regs *regs,
            unsigned long stack_size,
            TIDPTR_T *parent_tidptr,
            TIDPTR_T *child_tidptr)
{
  struct chos_link *parent_link;
  struct task_struct *t;
  PID_T pid;

  parent_link = lookup_link(current);

  pid=jumper(clone_flags, stack_start, regs, stack_size, 
		parent_tidptr, child_tidptr); 
  if ( pid > 0){
    write_lock_irq(tasklist_lock_p);
#ifdef PID_NS
    t=s_find_task_by_pid_ns(pid,current->nsproxy->pid_ns);
#else
    t=find_task_by_pid(pid);
#endif
    write_unlock_irq(tasklist_lock_p);
    if(t != NULL ) {
        set_link(parent_link,t);
    }
  }
  else{
    lookup_link(current);
  }
  /* Call the jumper       */
  return pid;
}


DO_FORK_RET jumper(unsigned long clone_flags,
            unsigned long stack_start,
            struct pt_regs *regs,
            unsigned long stack_size,
            TIDPTR_T *parent_tidptr,
            TIDPTR_T *child_tidptr)
{
   /* These are place holder instructions to give plenty of room.
    * They should never be called.  If the do get called, something
    * went wrong.
    */
    printk("You shouldn't see this!!!\n");
    printk("You shouldn't see this!!!\n");
    printk("You shouldn't see this!!!\n");
    printk("You shouldn't see this!!!\n");
    printk("You shouldn't see this!!!\n");
    printk("You shouldn't see this!!!\n");
    return 0;
}
#endif

#ifdef SCT
long my_sys_exit(int error_code)
{
  struct task_struct *task;
  struct list_head *list;

  list_for_each(list, &current->children) {
    task = list_entry(list, struct task_struct, sibling);
    lookup_link(task);
  }
  return orig_sys_exit(error_code);
}

void my_sys_exit_group(int error_code)
{
  struct task_struct *task;
  struct list_head *list;

  list_for_each(list, &current->children) {
    task = list_entry(list, struct task_struct, sibling);
    lookup_link(task);
  }
  orig_sys_exit_group(error_code);
}
#endif
