#ifndef MODULE
#define MODULE
#define __KERNEL__
#define __SMP__
#endif

/* some constants used in our module */
#define MODULE_NAME "chos"
#define MY_MODULE_VERSION "0.09"

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
 *  0.08 - Fix for follow_link to use ERR_PTR macro
 *  0.09 - Remove exception for root in setting the link
 *
 */


#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/version.h>
#include <linux/proc_fs.h> /* contains all procfs methods signature */
#include <linux/err.h>
#include <linux/list.h>
#include <linux/vmalloc.h>
#include <asm/unistd.h>
#include <asm/uaccess.h>
#include <asm/cacheflush.h>
#include <asm/pgtable.h>
#include <linux/list.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,99)
#include <linux/moduleparam.h>
#include <linux/namei.h>
#endif
#include <asm/desc.h>

#include "chos.h"
#include "address.h"

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
#endif
#if defined(START_ADD) && defined(LENGTH)
#define WRAP_DOFORK
#define END_ADD (START_ADD+LENGTH)
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
int chos_do_fork(unsigned long clone_flags,
            unsigned long stack_start,
            struct pt_regs *regs,
            unsigned long stack_size,
            int *parent_tidptr,
            int *child_tidptr);
int jumper(unsigned long clone_flags,
            unsigned long stack_start,
            struct pt_regs *regs,
            unsigned long stack_size,
            int *parent_tidptr,
            int *child_tidptr);

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

  BUG_ON(t->pid > ch->pid_max);

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

  write_lock_irq(&tasklist_lock);
  for (i=0;i<ch->pid_max;i++){
    t=find_task_by_pid(i);
    if (ch->procs[i].link!=NULL && t==NULL){
      reset_link(&(ch->procs[i]));
      count++;
    }
    else if (ch->procs[i].link!=NULL && ch->procs[i].start_time!=t->start_time.tv_sec){
      reset_link(&(ch->procs[i]));
      count++;
    }
  }
  write_unlock_irq(&tasklist_lock);
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
  if (ch->named.mnt==current->fs->rootmnt && ch->named.dentry->d_inode==current->fs->root->d_inode){
    retval=1;
  }
  return retval;
}

int my_chroot(void)
{
  int capback;
  int retval;
  mm_segment_t mem;

  capback=current->cap_effective;
  cap_raise(current->cap_effective,CAP_SYS_CHROOT);
  mem=get_fs(); 
  set_fs(KERNEL_DS);
  if ((retval=sys_chroot(CHOSROOT))!=0){
    printk("chroot failed\n");
  }
  current->cap_effective=capback;
  set_fs(mem);
  return retval;
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
  char *text;
  int i=0;

  i=0;
  while (i<(count) && buffer[i]!='\n' && buffer[i]!=0){
     i++;
  }
  text=(char *)kmalloc(i,GFP_KERNEL);
  if (text){
     __copy_from_user(text, buffer, i);
     text[i]=0;
  }
  else{
	  return -ENOMEM;
  }

  if (!is_valid_path(text)){
    printk("Attempt to use invalid path. uid=%d (Requested %s)\n",current->uid,text);
    return -ENOENT;
  }
//  printk("%s: is_valid_path: %d\n",text,is_valid_path(text));
//  printk("uid: %d euid: %d suid:%d fsuid:%d\n",current->uid,current->euid,current->suid,current->fsuid);
  MOD_INC;
  cleanup_links();
  link=create_link(text);
  set_link(link,current);
  if (!is_chrooted()){
    my_chroot();
  }
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
  len = sprintf(page, "%s %s %s\n",MODULE_NAME,MY_MODULE_VERSION,text_fork);
  *eof = 1;
  MOD_DEC;
  return len; /* return number of bytes returned */
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

int write_valid(struct file* file, const char* buffer, unsigned long count, void* data)
{
  char *path;
  int i;

  if (current->euid!=0){
    return -EPERM;
  }
  if (buffer[0]=='-'){
    printk("Reseting\n");
    resetvalid();
  }
  else{
   i=0;
   while (i<(count) && buffer[i]!='\n' && buffer[i]!=0)
     i++;
   path=(char *)kmalloc(i,GFP_KERNEL);
   if (path){
     path[i]=0;
     __copy_from_user(path, buffer, i);
     add_valid_path(path,i);
   }
   else{
     return 0;
   }
  }
  return count;
}

int read_valid(char* page, char** start, off_t off, int count, int* eof, void* data)
{
  int len=0;
  int len2=0;
  struct valid_path *c;
  struct list_head *p;
  char *ptr=page;

  for (p=valid_paths.next;p!=&valid_paths;){
    c=list_entry(p,struct valid_path,list);
    if (len<count){
      len2=sprintf(ptr,"%s\n",c->path);
      ptr+=len2;
      len+=len2;
    }
    p=p->next;
  }
  *eof=1;
  return len;
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
    printk("Unable to allocate chos handle\n");
    return -1;
  }

  retval=path_lookup(CHOSROOT, LOOKUP_FOLLOW | LOOKUP_DIRECTORY | LOOKUP_NOALT,&(ch->named)); 
  
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
  printk("sys_call_table=%lx\n",(unsigned long)sys_call_table);
  printk("orig exit=%lx\n",(unsigned long)orig_sys_exit);
  printk("new exit=%lx\n",(unsigned long)my_sys_exit);
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
  struct proc_dir_entry *validfile;

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
  savestatefile = create_proc_entry("savestate", 0600, dir);
  if (savestatefile == NULL) goto fail_entry;
  savestatefile->write_proc = write_savestate;

  /* This is used to read the version. */
  versfile = create_proc_entry("version", 0444, dir);
  if (versfile == NULL) goto fail_entry;
  versfile->read_proc = read_version;

  /* This is used to read the version. */
  validfile = create_proc_entry("valid", 0600, dir);
  if (validfile == NULL) goto fail_entry;
  validfile->read_proc = read_valid;
  validfile->write_proc = write_valid;

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
    path_release(&(ch->named));
    vfree(ch->procs);
    kfree(ch);
  }
  else{
    printk("State saved at 0x%lx\n",(long)ch);
  }
  printk("Module cleanup. Chos entry removed.\n");
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
      printk("Mismatch to trap do_fork %x %x\n",*ptr,opcode[i]);
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

int chos_do_fork(unsigned long clone_flags,
            unsigned long stack_start,
            struct pt_regs *regs,
            unsigned long stack_size,
            int *parent_tidptr,
            int *child_tidptr)
{
  struct task_struct *t;
  int pid;

  pid=jumper(clone_flags, stack_start, regs, stack_size, 
		parent_tidptr, child_tidptr); 
  if ( pid > 0){
    write_lock_irq(&tasklist_lock);
    t=find_task_by_pid(pid);
    write_unlock_irq(&tasklist_lock);
    lookup_link(t);
  }
  else{
    lookup_link(current);
  }
  /* Call the jumper       */
  return pid;
}


int jumper(unsigned long clone_flags,
            unsigned long stack_start,
            struct pt_regs *regs,
            unsigned long stack_size,
            int *parent_tidptr,
            int *child_tidptr)
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
