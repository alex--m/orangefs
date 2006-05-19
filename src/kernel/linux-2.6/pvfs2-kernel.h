/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/** \defgroup pvfs2linux PVFS2 Linux kernel support
 *
 *  The PVFS2 Linux kernel support allows PVFS2 volumes to be mounted and
 *  accessed through the Linux VFS (i.e. using standard I/O system calls).
 *  This support is only needed on clients that wish to mount the file system.
 *
 * @{
 */

/** \file
 *  Declarations and macros for the PVFS2 Linux kernel support.
 */

#ifndef __PVFS2KERNEL_H
#define __PVFS2KERNEL_H

#include <linux/config.h>

#ifdef PVFS2_LINUX_KERNEL_2_4

/* the 2.4 kernel requires us to manually set up modversions if needed */
#if CONFIG_MODVERSIONS==1
#define MODVERSIONS
#include <linux/modversions.h>
#endif 

#define __NO_VERSION__
#include <linux/version.h>
#include <linux/module.h>

#ifndef HAVE_SECTOR_T
typedef unsigned long sector_t;
#endif

#else /* !(PVFS2_LINUX_KERNEL_2_4) */

#include <linux/moduleparam.h>
#include <linux/vermagic.h>
#include <linux/statfs.h>
#include <linux/buffer_head.h>
#include <linux/backing-dev.h>
#include <linux/mpage.h>
#include <linux/namei.h>
#include <linux/errno.h>

#endif /* PVFS2_LINUX_KERNEL_2_4 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/fs.h>

#include "pvfs2-config.h"
#ifdef HAVE_AIO
#include <linux/aio.h>
#endif
#ifdef HAVE_POSIX_ACL_H
#include <linux/posix_acl.h>
#endif
#ifdef HAVE_POSIX_ACL_XATTR_H
#include <linux/posix_acl_xattr.h>
#endif
#ifdef HAVE_LINUX_COMPAT_H
#include <linux/compat.h>
#endif
#ifdef HAVE_LINUX_IOCTL32_H
#include <linux/ioctl32.h>
#endif
#ifdef HAVE_LINUX_SYSCALLS_H
#include <linux/syscalls.h>
#endif
#include <asm/uaccess.h>
#include <asm/atomic.h>
#include <linux/uio.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <asm/atomic.h>
#include <linux/smp_lock.h>
#include <linux/wait.h>
#include <linux/dcache.h>
#include <linux/pagemap.h>
#include <linux/poll.h>
#include <linux/rwsem.h>
#include <asm/unaligned.h>
#ifdef HAVE_ASM_IOCTL32_H
#include <asm/ioctl32.h>
#endif

#ifdef HAVE_XATTR
#include <linux/xattr.h>
#ifdef HAVE_LINUX_XATTR_ACL_H
#include <linux/xattr_acl.h>
#endif
#endif

/* taken from include/linux/fs.h from 2.4.19 or later kernels */
#ifndef MAX_LFS_FILESIZE
#if BITS_PER_LONG == 32
#define MAX_LFS_FILESIZE     (((u64)PAGE_CACHE_SIZE << (BITS_PER_LONG))-1)
#elif BITS_PER_LONG == 64
#define MAX_LFS_FILESIZE     0x7fffffffffffffff
#endif
#endif /* MAX_LFS_FILESIZE */

#include "pint-dev-shared.h"
#include "pvfs2-dev-proto.h"
#include "pvfs2-types.h"

#define pvfs2_error printk

#ifdef PVFS2_KERNEL_DEBUG
#define pvfs2_print printk
#define pvfs2_panic(msg)                                       \
do {                                                           \
    pvfs2_error("BUG! Please contact pvfs2-developers@beowulf-"\
                "underground.org\n");                          \
    panic(msg);                                                \
} while(0)
#else
#define pvfs2_print(format...) do{                             \
    if(debug) printk(format);                                  \
}while(0)
#define pvfs2_panic(msg)                                       \
do {                                                           \
    pvfs2_error("BUG! Please contact pvfs2-developers@beowulf-"\
                "underground.org\n");                          \
    pvfs2_error(msg);                                          \
} while(0)
#endif

/*
  this attempts to disable the annotations used by the 'sparse' kernel
  source utility on systems that can't understand it by defining the
  used annotations away
*/
#ifndef __user
#define __user
#endif

#ifdef PVFS2_KERNEL_DEBUG
#define PVFS2_DEFAULT_OP_TIMEOUT_SECS       30
#else
#define PVFS2_DEFAULT_OP_TIMEOUT_SECS       60
#endif

#define PVFS2_REQDEVICE_NAME          "pvfs2-req"

#define PVFS2_DEVREQ_MAGIC             0x20030529
#define PVFS2_LINK_MAX                 0x000000FF
#define PVFS2_OP_RETRY_COUNT           0x00000005
#define PVFS2_SEEK_END                 0x00000002
#define PVFS2_MAX_NUM_OPTIONS          0x00000004
#define PVFS2_MAX_MOUNT_OPT_LEN        0x00000080
#define PVFS2_NUM_READDIR_RETRIES      0x0000000A

#define MAX_DEV_REQ_UPSIZE (2*sizeof(int32_t) +   \
sizeof(uint64_t) + sizeof(pvfs2_upcall_t))
#define MAX_DEV_REQ_DOWNSIZE (2*sizeof(int32_t) + \
sizeof(uint64_t) + sizeof(pvfs2_downcall_t))

#define BITS_PER_LONG_DIV_8 (BITS_PER_LONG >> 3)

#define MAX_ALIGNED_DEV_REQ_UPSIZE                  \
(MAX_DEV_REQ_UPSIZE +                               \
((((MAX_DEV_REQ_UPSIZE / (BITS_PER_LONG_DIV_8)) *   \
   (BITS_PER_LONG_DIV_8)) +                         \
    (BITS_PER_LONG_DIV_8)) - MAX_DEV_REQ_UPSIZE))
#define MAX_ALIGNED_DEV_REQ_DOWNSIZE                \
(MAX_DEV_REQ_DOWNSIZE +                             \
((((MAX_DEV_REQ_DOWNSIZE / (BITS_PER_LONG_DIV_8)) * \
   (BITS_PER_LONG_DIV_8)) +                         \
    (BITS_PER_LONG_DIV_8)) - MAX_DEV_REQ_DOWNSIZE))

/* borrowed from irda.h */
#ifndef MSECS_TO_JIFFIES
#define MSECS_TO_JIFFIES(ms) (((ms)*HZ+999)/1000)
#endif

/* translates an inode number to a pvfs2 handle */
#define pvfs2_ino_to_handle(ino) (PVFS_handle)ino

/* translates a pvfs2 handle to an inode number */
#define pvfs2_handle_to_ino(handle) (ino_t)pvfs2_handle_l32(handle)

#define pvfs2_handle_l32(handle) (__u32)(handle)
#define pvfs2_handle_h32(handle) (__u32)(handle >> 32)

/************************************
 * valid pvfs2 kernel operation states
 *
 * unknown  - op was just initialized
 * waiting  - op is on request_list (upward bound)
 * inprogr  - op is in progress (waiting for downcall)
 * serviced - op has matching downcall; ok
 ************************************/
#define PVFS2_VFS_STATE_UNKNOWN        0x00FF0000
#define PVFS2_VFS_STATE_WAITING        0x00FF0001
#define PVFS2_VFS_STATE_INPROGR        0x00FF0002
#define PVFS2_VFS_STATE_SERVICED       0x00FF0003

/* Defines for incrementing aio_ref_count */
#ifndef HAVE_AIO_VFS_SUPPORT
#define get_op(op)
#define put_op(op) op_release(op)
#define op_wait(op)
#else
#define get_op(op) \
    do {\
        atomic_inc(&(op)->aio_ref_count);\
        pvfs2_print("Alloced OP (%p:%ld)\n", op, (unsigned long)(op)->tag);\
    } while(0)
#define put_op(op) \
    do {\
        if (atomic_sub_and_test(1, &(op)->aio_ref_count) == 1) {\
            pvfs2_print("Releasing OP (%p:%ld)\n", op, (unsigned long)(op)->tag);\
            op_release(op);\
        }\
    } while(0)
#define op_wait(op) (atomic_read(&(op)->aio_ref_count) <= 2 ? 0 : 1)
#endif

/* Defines for controlling whether I/O upcalls are for async or sync operations */
enum PVFS_async_io_type 
{
     PVFS_VFS_SYNC_IO = 0,
    PVFS_VFS_ASYNC_IO = 1,
};

/************************************
 * pvfs2 kernel memory related flags
 ************************************/

#if ((defined PVFS2_KERNEL_DEBUG) && (defined CONFIG_DEBUG_SLAB))
#define PVFS2_CACHE_CREATE_FLAGS SLAB_RED_ZONE
#else
#define PVFS2_CACHE_CREATE_FLAGS 0
#endif /* ((defined PVFS2_KERNEL_DEBUG) && (defined CONFIG_DEBUG_SLAB)) */

#define PVFS2_CACHE_ALLOC_FLAGS (SLAB_KERNEL)
#define PVFS2_GFP_FLAGS (GFP_KERNEL)
#define PVFS2_BUFMAP_GFP_FLAGS (GFP_KERNEL)

#ifdef CONFIG_HIGHMEM
#define pvfs2_kmap(page) kmap(page)
#define pvfs2_kunmap(page) kunmap(page)
#else
#define pvfs2_kmap(page) page_address(page)
#define pvfs2_kunmap(page) do {} while(0)
#endif /* CONFIG_HIGHMEM */

/* pvfs2 xattr and acl related defines */
#ifdef HAVE_XATTR
#define PVFS2_XATTR_INDEX_POSIX_ACL_ACCESS  1
#define PVFS2_XATTR_INDEX_POSIX_ACL_DEFAULT 2
#define PVFS2_XATTR_INDEX_TRUSTED           3
#define PVFS2_XATTR_INDEX_DEFAULT           4

#ifndef POSIX_ACL_XATTR_ACCESS
#define POSIX_ACL_XATTR_ACCESS	"system.posix_acl_access"
#endif
#ifndef POSIX_ACL_XATTR_DEFAULT
#define POSIX_ACL_XATTR_DEFAULT	"system.posix_acl_default"
#endif

#define PVFS2_XATTR_NAME_ACL_ACCESS  POSIX_ACL_XATTR_ACCESS
#define PVFS2_XATTR_NAME_ACL_DEFAULT POSIX_ACL_XATTR_DEFAULT
#define PVFS2_XATTR_NAME_TRUSTED_PREFIX "trusted."
#define PVFS2_XATTR_NAME_DEFAULT_PREFIX ""

#if !defined(PVFS2_LINUX_KERNEL_2_4) && defined(HAVE_GENERIC_GETXATTR)

extern int pvfs2_acl_chmod(struct inode *inode);
extern int pvfs2_init_acl(struct inode *inode, struct inode *dir);

extern struct xattr_handler *pvfs2_xattr_handlers[];
extern struct xattr_handler pvfs2_xattr_acl_default_handler, pvfs2_xattr_acl_access_handler;
extern struct xattr_handler pvfs2_xattr_trusted_handler;
extern struct xattr_handler pvfs2_xattr_default_handler;

typedef struct {
    int32_t p_tag;
    uint32_t p_perm;
    uint32_t p_id;
} pvfs2_acl_entry;

#endif

static inline int convert_to_internal_xattr_flags(int setxattr_flags)
{
    int internal_flag = 0;

    /* Attribute must exist! */
    if (setxattr_flags & XATTR_REPLACE)
    {
        internal_flag = PVFS_XATTR_REPLACE;
    }
    /* Attribute must not exist */
    else if (setxattr_flags & XATTR_CREATE)
    {
        internal_flag = PVFS_XATTR_CREATE;
    }
    return internal_flag;
}

int pvfs2_xattr_set_trusted(struct inode *inode, 
    const char *name, const void *buffer, size_t size, int flags);
int pvfs2_xattr_get_trusted(struct inode *inode,
    const char *name, void *buffer, size_t size);
int pvfs2_xattr_set_default(struct inode *inode, 
    const char *name, const void *buffer, size_t size, int flags);
int pvfs2_xattr_get_default(struct inode *inode,
    const char *name, void *buffer, size_t size);


#endif

/************************************
 * pvfs2 data structures
 ************************************/
typedef struct
{
    int op_state;
    uint64_t tag;

    pvfs2_upcall_t upcall;
    pvfs2_downcall_t downcall;

    wait_queue_head_t waitq;
    spinlock_t lock;

    int io_completed;
    wait_queue_head_t io_completion_waitq;
    void *priv;/* used by the async I/O code to stash the pvfs2_kiocb structure */
    atomic_t aio_ref_count; /* used again for the async I/O code for deallocation */

    struct list_head list;
} pvfs2_kernel_op_t;

/** per inode private pvfs2 info */
typedef struct
{
    PVFS_object_ref refn;
    int num_readdir_retries;
    uint64_t directory_version;
    char *link_target;
    /*
     * Reading/Writing Extended attributes need to acquire the appropriate
     * reader/writer semaphore on the pvfs2_inode_t structure.
     */
    struct rw_semaphore xattr_sem;

#ifdef PVFS2_LINUX_KERNEL_2_4
    struct inode *vfs_inode;
#else
    struct inode vfs_inode;
#endif
    sector_t last_failed_block_index_read;
    int error_code;
} pvfs2_inode_t;

/** mount options.  only accepted mount options are listed.
 */
typedef struct
{
    /** intr option (if set) is inspired by the nfs intr option that
     *  interrupts the operation in progress if a signal is received,
     *  and ignores the signal otherwise (if not set).
     */
    int intr;
    /** acl option (if set) is inspired by the ext2 acl option that
     * requires the file system to honor acl's 
     */
    int acl;
} pvfs2_mount_options_t;

/** per superblock private pvfs2 info */
typedef struct
{
    PVFS_handle root_handle;
    PVFS_fs_id fs_id;
    int id;
    pvfs2_mount_options_t mnt_options;
    char data[PVFS2_MAX_MOUNT_OPT_LEN];
    char devname[PVFS_MAX_SERVER_ADDR_LEN];
    struct super_block *sb;

    struct list_head list;
} pvfs2_sb_info_t;

/** a temporary structure used only for sb mount time that groups the
 *  mount time data provided along with a private superblock structure
 *  that is allocated before a 'kernel' superblock is allocated.
*/
typedef struct
{
    void *data;
    PVFS_handle root_handle;
    PVFS_fs_id fs_id;
    int id;
} pvfs2_mount_sb_info_t;

#ifdef HAVE_AIO_VFS_SUPPORT

/** structure that holds the state of any async I/O operation issued 
 *  through the VFS. Needed especially to handle cancellation requests
 *  or even completion notification so that the VFS client-side daemon
 *  can free up its vfs_request slots.
 */
typedef struct
{
    struct task_struct *tsk;/* the pointer to the task that initiated the AIO */
    struct kiocb *kiocb; /* pointer to the kiocb that kicked this operation */
    int buffer_index; /* buffer index that was used for the I/O */
    pvfs2_kernel_op_t *op; /* pvfs2 kernel operation type */
    char __user *buffer; /* The user space buffer to which I/O is being staged */
    int   rw; /* set to indicate the type of the operation */
    loff_t offset; /* file offset */
    size_t bytes_to_be_copied; /* and the count in bytes */
    ssize_t bytes_copied;
    int needs_cleanup;
} pvfs2_kiocb;

#endif

/*
  NOTE: See Documentation/filesystems/porting for information
  on implementing FOO_I and properly accessing fs private data
*/
static inline pvfs2_inode_t *PVFS2_I(
    struct inode *inode)
{
#ifdef PVFS2_LINUX_KERNEL_2_4
    return (pvfs2_inode_t *)inode->u.generic_ip;
#else
    return container_of(inode, pvfs2_inode_t, vfs_inode);
#endif
}

static inline pvfs2_sb_info_t *PVFS2_SB(
    struct super_block *sb)
{
#ifdef PVFS2_LINUX_KERNEL_2_4
    return (pvfs2_sb_info_t *)sb->u.generic_sbp;
#else
    return (pvfs2_sb_info_t *)sb->s_fs_info;
#endif
}

/****************************
 * defined in pvfs2-cache.c
 ****************************/
int op_cache_initialize(void);
int op_cache_finalize(void);
pvfs2_kernel_op_t *op_alloc(void);
void op_release(pvfs2_kernel_op_t *op);

int dev_req_cache_initialize(void);
int dev_req_cache_finalize(void);
void *dev_req_alloc(void);
void  dev_req_release(void *);

int pvfs2_inode_cache_initialize(void);
int pvfs2_inode_cache_finalize(void);
pvfs2_inode_t *pvfs2_inode_alloc(void);
void pvfs2_inode_release(pvfs2_inode_t *);

#ifdef HAVE_AIO_VFS_SUPPORT
int kiocb_cache_initialize(void);
int kiocb_cache_finalize(void);
pvfs2_kiocb* kiocb_alloc(void);
void kiocb_release(pvfs2_kiocb *ptr);
#else
static inline int kiocb_cache_initialize(void)
{
    return 0;
}
static inline int kiocb_cache_finalize(void)
{
    return 0;
}
#endif

/****************************
 * defined in waitqueue.c
 ****************************/
int wait_for_matching_downcall(
    pvfs2_kernel_op_t * op);
int wait_for_cancellation_downcall(
    pvfs2_kernel_op_t * op);
void clean_up_interrupted_operation(
    pvfs2_kernel_op_t * op);

/****************************
 * defined in super.c
 ****************************/
#ifdef PVFS2_LINUX_KERNEL_2_4
struct super_block* pvfs2_get_sb(
    struct super_block *sb,
    void *data,
    int silent);
#else
struct super_block *pvfs2_get_sb(
    struct file_system_type *fst, int flags,
    const char *devname, void *data);
#endif

int pvfs2_remount(
    struct super_block *sb,
    int *flags,
    char *data);

/****************************
 * defined in inode.c
 ****************************/
struct inode *pvfs2_get_custom_inode(
    struct super_block *sb,
    struct inode *dir,
    int mode,
    dev_t dev,
    unsigned long ino);

int pvfs2_setattr(
    struct dentry *dentry,
    struct iattr *iattr);

#ifdef PVFS2_LINUX_KERNEL_2_4
int pvfs2_revalidate(
    struct dentry *dentry);
#else
int pvfs2_getattr(
    struct vfsmount *mnt,
    struct dentry *dentry,
    struct kstat *kstat);
#endif

/****************************
 * defined in xattr.c
 ****************************/
#ifdef HAVE_SETXATTR_CONST_ARG
int pvfs2_setxattr(struct dentry *dentry, const char *name,
		const void *value, size_t size, int flags);
#else
int pvfs2_setxattr(struct dentry *dentry, const char *name,
		void *value, size_t size, int flags);
#endif
ssize_t pvfs2_getxattr(struct dentry *dentry, const char *name,
		         void *buffer, size_t size);
ssize_t pvfs2_listxattr(struct dentry *dentry, char *buffer, size_t size);
int pvfs2_removexattr(struct dentry *dentry, const char *name);

/****************************
 * defined in namei.c
 ****************************/
#ifdef PVFS2_LINUX_KERNEL_2_4
struct dentry *pvfs2_lookup(
    struct inode *dir,
    struct dentry *dentry);
int pvfs2_permission(struct inode *, int);
#else
struct dentry *pvfs2_lookup(
    struct inode *dir,
    struct dentry *dentry,
    struct nameidata *nd);
int pvfs2_permission(struct inode *inode, 
        int mask, struct nameidata *nd);
#endif

/*****************************
 * defined in devpvfs2-req.c
 ****************************/

int     pvfs2_ioctl32_init(void);
void    pvfs2_ioctl32_cleanup(void);


/****************************
 * defined in pvfs2-utils.c
 ****************************/
int pvfs2_gen_credentials(
    PVFS_credentials *credentials);

ssize_t pvfs2_inode_getxattr(
        struct inode *inode, const char* prefix,
        const char *name, void *buffer, size_t size);
int pvfs2_inode_setxattr(struct inode *inode, const char* prefix,
        const char *name, const void *value, size_t size, int flags);
int pvfs2_inode_removexattr(struct inode *inode, const char* prefix,
        const char *name);
int pvfs2_inode_listxattr(struct inode *inode, char *, size_t);

int pvfs2_inode_getattr(
    struct inode *inode, uint32_t mask);

int pvfs2_inode_setattr(
    struct inode *inode,
    struct iattr *iattr);

struct inode *pvfs2_create_entry(
    struct inode *dir,
    struct dentry *dentry,
    const char *symname,
    int mode,
    int op_type,
    int *error_code);

int pvfs2_remove_entry(
    struct inode *dir,
    struct dentry *dentry);

int pvfs2_truncate_inode(
    struct inode *inode,
    loff_t size);

void pvfs2_inode_initialize(
    pvfs2_inode_t *pvfs2_inode);

void pvfs2_inode_finalize(
    pvfs2_inode_t *pvfs2_inode);

void pvfs2_op_initialize(
    pvfs2_kernel_op_t *op);

void pvfs2_make_bad_inode(
    struct inode *inode);

void mask_blocked_signals(
    sigset_t *orig_sigset);

void unmask_blocked_signals(
    sigset_t *orig_sigset);

#ifdef USE_MMAP_RA_CACHE
int pvfs2_flush_mmap_racache(
    struct inode *inode);
#endif

int pvfs2_unmount_sb(
    struct super_block *sb);

int pvfs2_cancel_op_in_progress(
    unsigned long tag);

PVFS_time pvfs2_convert_time_field(
    void *time_ptr);

int pvfs2_normalize_to_errno(PVFS_error error_code);

/************************************
 * misc convenience macros
 ************************************/
extern struct semaphore request_semaphore;

#define add_op_to_request_list(op)                           \
do {                                                         \
    spin_lock(&op->lock);                                    \
    op->op_state = PVFS2_VFS_STATE_WAITING;                  \
                                                             \
    spin_lock(&pvfs2_request_list_lock);                     \
    list_add_tail(&op->list, &pvfs2_request_list);           \
    spin_unlock(&pvfs2_request_list_lock);                   \
                                                             \
    spin_unlock(&op->lock);                                  \
    wake_up_interruptible(&pvfs2_request_list_waitq);        \
} while(0)

#define add_priority_op_to_request_list(op)                  \
do {                                                         \
    spin_lock(&op->lock);                                    \
    op->op_state = PVFS2_VFS_STATE_WAITING;                  \
                                                             \
    spin_lock(&pvfs2_request_list_lock);                     \
    list_add(&op->list, &pvfs2_request_list);                \
    spin_unlock(&pvfs2_request_list_lock);                   \
                                                             \
    spin_unlock(&op->lock);                                  \
    wake_up_interruptible(&pvfs2_request_list_waitq);        \
} while(0)

#define remove_op_from_request_list(op)                      \
do {                                                         \
    struct list_head *tmp = NULL;                            \
    pvfs2_kernel_op_t *tmp_op = NULL;                        \
                                                             \
    spin_lock(&pvfs2_request_list_lock);                     \
    list_for_each(tmp, &pvfs2_request_list) {                \
        tmp_op = list_entry(tmp, pvfs2_kernel_op_t, list);   \
        if (tmp_op && (tmp_op == op)) {                      \
            list_del(&tmp_op->list);                         \
            break;                                           \
        }                                                    \
    }                                                        \
    spin_unlock(&pvfs2_request_list_lock);                   \
} while(0)

#define remove_op_from_htable_ops_in_progress(op)            \
do {                                                         \
    qhash_search_and_remove(htable_ops_in_progress,          \
                            &(op->tag));                     \
} while(0)

#define PVFS2_OP_INTERRUPTIBLE 1  /**< service_operation() is interruptible */
#define PVFS2_OP_PRIORITY 2       /**< service_operation() is high priority */
#define PVFS2_OP_CANCELLATION 4   /**< this is a cancellation */
#define PVFS2_OP_NO_SEMAPHORE 8   /**< don't acquire semaphore */

int service_operation(pvfs2_kernel_op_t* op, const char* op_name, 
    int num_retries, int flags);

/** handles two possible error cases, depending on context.
 *
 *  by design, our vfs i/o errors need to be handled in one of two ways,
 *  depending on where the error occured.
 *
 *  if the error happens in the waitqueue code because we either timed
 *  out or a signal was raised while waiting, we need to cancel the
 *  userspace i/o operation and free the op manually.  this is done to
 *  avoid having the device start writing application data to our shared
 *  bufmap pages without us expecting it.
 *
 *  FIXME: POSSIBLE OPTIMIZATION:
 *  However, if we timed out or if we got a signal AND our upcall was never
 *  picked off the queue (i.e. we were in PVFS2_VFS_STATE_WAITING), then we don't
 *  need to send a cancellation upcall. The way we can handle this is set error_exit
 *  to 2 in such cases and 1 whenever cancellation has to be sent and have handle_error
 *  take care of this situation as well..
 *
 *  if a pvfs2 sysint level error occured and i/o has been completed,
 *  there is no need to cancel the operation, as the user has finished
 *  using the bufmap page and so there is no danger in this case.  in
 *  this case, we wake up the device normally so that it may free the
 *  op, as normal.
 *
 *  \note the only reason this is a macro is because both read and write
 *  cases need the exact same handling code.
 */
#define handle_io_error()                                 \
do {                                                      \
    if(new_op->op_state != PVFS2_VFS_STATE_SERVICED)      \
    {                                                     \
        pvfs2_cancel_op_in_progress(new_op->tag);         \
        op_release(new_op);                               \
    }                                                     \
    else                                                  \
    {                                                     \
        wake_up_device_for_return(new_op);                \
    }                                                     \
    pvfs_bufmap_put(buffer_index);                        \
    *offset = original_offset;                            \
} while(0)

#ifdef HAVE_AIO_VFS_SUPPORT
/* 
 * This macro differs from the above only in that it does not
 * manipulate the offsets.
 */
#define handle_sync_aio_error()                           \
do {                                                      \
    if(new_op->op_state != PVFS2_VFS_STATE_SERVICED)      \
    {                                                     \
        pvfs2_cancel_op_in_progress(new_op->tag);         \
        op_release(new_op);                               \
    }                                                     \
    else                                                  \
    {                                                     \
        wake_up_device_for_return(new_op);                \
    }                                                     \
    pvfs_bufmap_put(buffer_index);                        \
} while(0)


/** All this function does is that it tries to issue the operation and return
 * by queuing it up in the device queue.
 *  \note used in pvfs_file_aio_read/pvfs_file_aio_write functions
 *  alone
 */
#define service_async_vfs_op(op)                          \
do {                                                      \
    down(&request_semaphore);                             \
    add_op_to_request_list(op);                           \
    up(&request_semaphore);                               \
} while(0)

#endif

#define get_interruptible_flag(inode)                     \
((PVFS2_SB(inode->i_sb)->mnt_options.intr ? PVFS2_OP_INTERRUPTIBLE : 0))

#define get_acl_flag(inode)                               \
(PVFS2_SB(inode->i_sb)->mnt_options.acl)

#ifdef USE_MMAP_RA_CACHE
#define clear_inode_mmap_ra_cache(inode)                  \
do {                                                      \
  pvfs2_print("calling clear_inode_mmap_ra_cache on %d\n",\
              (int)inode->i_ino);                         \
  pvfs2_flush_mmap_racache(inode);                        \
  pvfs2_print("clear_inode_mmap_ra_cache finished\n");    \
} while(0)
#else
#define clear_inode_mmap_ra_cache(inode)
#endif /* USE_MMAP_RA_CACHE */

#define add_pvfs2_sb(sb)                                             \
do {                                                                 \
    pvfs2_print("Adding SB %p to pvfs2 superblocks\n", PVFS2_SB(sb));\
    spin_lock(&pvfs2_superblocks_lock);                              \
    list_add_tail(&PVFS2_SB(sb)->list, &pvfs2_superblocks);          \
    spin_unlock(&pvfs2_superblocks_lock);                            \
} while(0)

#define remove_pvfs2_sb(sb)                                          \
do {                                                                 \
    struct list_head *tmp = NULL;                                    \
    pvfs2_sb_info_t *pvfs2_sb = NULL;                                \
                                                                     \
    spin_lock(&pvfs2_superblocks_lock);                              \
    list_for_each(tmp, &pvfs2_superblocks) {                         \
        pvfs2_sb = list_entry(tmp, pvfs2_sb_info_t, list);           \
        if (pvfs2_sb && (pvfs2_sb->sb == sb)) {                      \
            pvfs2_print("Removing SB %p from pvfs2 superblocks\n",   \
                        pvfs2_sb);                                   \
            list_del(&pvfs2_sb->list);                               \
            break;                                                   \
        }                                                            \
    }                                                                \
    spin_unlock(&pvfs2_superblocks_lock);                            \
} while(0)

#define pvfs2_update_inode_time(inode) \
do { inode->i_mtime = inode->i_ctime = CURRENT_TIME; } while(0)


#ifdef PVFS2_LINUX_KERNEL_2_4
#define get_block_block_type long
#define pvfs2_lock_inode(inode) do {} while(0)
#define pvfs2_unlock_inode(inode) do {} while(0)
#define pvfs2_d_splice_alias(dentry, inode) d_add(dentry, inode)
#define pvfs2_kernel_readpage block_read_full_page

/*
  redhat 9 2.4.x kernels have to be treated almost like 2.6.x kernels
  so we special case them here
*/
#ifdef REDHAT_RELEASE_9
#define pvfs2_current_signal_lock current->sighand->siglock
#define pvfs2_current_sigaction current->sighand->action
#define pvfs2_recalc_sigpending recalc_sigpending
#define pvfs2_set_page_reserved(page) do {} while(0)
#define pvfs2_clear_page_reserved(page) do {} while(0)
#else
#define pvfs2_current_signal_lock current->sigmask_lock
#define pvfs2_current_sigaction current->sig->action
#define pvfs2_recalc_sigpending() recalc_sigpending(current)
#define pvfs2_set_page_reserved(page) SetPageReserved(page)
#define pvfs2_clear_page_reserved(page) \
do { ClearPageReserved(page); put_page(page); } while(0)
#endif /* REDHAT_RELEASE_9 */

#define fill_default_sys_attrs(sys_attr,type,mode)\
do                                                \
{                                                 \
    time_t cur_time = CURRENT_TIME;               \
    sys_attr.owner = current->fsuid;              \
    sys_attr.group = current->fsgid;              \
    sys_attr.atime =                              \
      pvfs2_convert_time_field((void *)&cur_time);\
    sys_attr.mtime =                              \
      pvfs2_convert_time_field((void *)&cur_time);\
    sys_attr.ctime =                              \
      pvfs2_convert_time_field((void *)&cur_time);\
    sys_attr.size = 0;                            \
    sys_attr.perms = PVFS2_translate_mode(mode);  \
    sys_attr.objtype = type;                      \
    sys_attr.mask = PVFS_ATTR_SYS_ALL_SETABLE;    \
} while(0)

#else /* !(PVFS2_LINUX_KERNEL_2_4) */

#define get_block_block_type sector_t
#define pvfs2_lock_inode(inode) spin_lock(&inode->i_lock)
#define pvfs2_unlock_inode(inode) spin_unlock(&inode->i_lock)
#define pvfs2_current_signal_lock current->sighand->siglock
#define pvfs2_current_sigaction current->sighand->action
#define pvfs2_recalc_sigpending recalc_sigpending
#define pvfs2_d_splice_alias(dentry, inode) d_splice_alias(inode, dentry)
#define pvfs2_kernel_readpage mpage_readpage
#define pvfs2_set_page_reserved(page) do {} while(0)
#define pvfs2_clear_page_reserved(page) do {} while(0)

#define fill_default_sys_attrs(sys_attr,type,mode)\
do                                                \
{                                                 \
    struct timespec cur_time = CURRENT_TIME;      \
    sys_attr.owner = current->fsuid;              \
    sys_attr.group = current->fsgid;              \
    sys_attr.atime =                              \
      pvfs2_convert_time_field((void *)&cur_time);\
    sys_attr.mtime =                              \
      pvfs2_convert_time_field((void *)&cur_time);\
    sys_attr.ctime =                              \
      pvfs2_convert_time_field((void *)&cur_time);\
    sys_attr.size = 0;                            \
    sys_attr.perms = PVFS2_translate_mode(mode);  \
    sys_attr.objtype = type;                      \
    sys_attr.mask = PVFS_ATTR_SYS_ALL_SETABLE;    \
} while(0)

#endif /* PVFS2_LINUX_KERNEL_2_4 */


/************************************
 * misc convenience functions
 ************************************/
static inline int pvfs2_internal_revalidate(
    struct inode *inode)
{
    int ret = -EINVAL;
    if (inode)
    {
        /*
         * The dentry revalidating function expects that all fields of the inode
         * would be refreshed, so we dont have much of a choice here too.
         */
        ret = ((pvfs2_inode_getattr(inode, PVFS_ATTR_SYS_ALL) == 0) ? 1 : 0);
#if 0
/* Calling make_bad_inode() here results in a bad reference count on the
 * inode.  It therefore gets cached until the module is unloaded, when we get 
 * a "VFS: Busy inodes after unmount. Self-destruct in 5 seconds." error
 * message.  It is better to just let it be cleaned up naturally after 
 * validation failure. -Phil
 */
        if (ret == 0)
        {
            pvfs2_make_bad_inode(inode);
        }
#endif
    }
    return ret;
}

#ifdef PVFS2_LINUX_KERNEL_2_4
/*
  based on code from 2.6.x's fs/libfs.c with required macro support
  from include/linux/list.h
*/
static inline int simple_positive(struct dentry *dentry)
{
    return dentry->d_inode && !d_unhashed(dentry);
}

#define list_for_each_entry(pos, head, member)             \
for (pos = list_entry((head)->next, typeof(*pos), member), \
  prefetch(pos->member.next);                              \
  &pos->member != (head);                                  \
  pos = list_entry(pos->member.next, typeof(*pos), member),\
  prefetch(pos->member.next))

static inline int simple_empty(struct dentry *dentry)
{
    struct dentry *child;
    int ret = 0;
    spin_lock(&dcache_lock);
    list_for_each_entry(child, &dentry->d_subdirs, d_child)
        if (simple_positive(child))
            goto out;
    ret = 1;
out:
    spin_unlock(&dcache_lock);
    return ret;
}

#if (PVFS2_LINUX_KERNEL_2_4_MINOR_VER < 19)
static inline int dcache_dir_open(struct inode *inode, struct file *file)
{
    static struct qstr cursor_name = {.len = 1, .name = "."};

    file->private_data = d_alloc(file->f_dentry, &cursor_name);

    return file->private_data ? 0 : -ENOMEM;
}

static inline int dcache_dir_close(struct inode *inode, struct file *file)
{
    dput(file->private_data);
    return 0;
}
#endif /* PVFS2_LINUX_KERNEL_2_4_MINOR_VER */

/* some 2.4 kernels backport a lot of stuff from 2.6, so we have to
 * feature-test instead of relying on kernel versions */
#ifndef HAVE_I_SIZE_READ
static inline loff_t i_size_read(struct inode *inode)
{
    return inode->i_size;
}
#endif

#ifndef HAVE_I_SIZE_WRITE
static inline void i_size_write(struct inode *inode, loff_t i_size)
{
    inode->i_size = i_size;
}
#endif

#ifndef HAVE_PARENT_INO
static inline ino_t parent_ino(struct dentry *dentry)
{
    return dentry->d_parent->d_inode->i_ino;
}
#endif

#endif /* PVFS2_LINUX_KERNEL_2_4 */

#endif /* __PVFS2KERNEL_H */

/* @} */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
