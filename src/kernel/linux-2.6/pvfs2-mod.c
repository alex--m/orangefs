/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * Changes by Acxiom Corporation to add proc file handler for pvfs2 client
 * parameters, Copyright � Acxiom Corporation, 2005.
 *
 * See COPYING in top-level directory.
 */

#include "pvfs2-kernel.h"
#include "pvfs2-proc.h"

#ifndef PVFS2_VERSION
#define PVFS2_VERSION "Unknown"
#endif

extern struct file_operations pvfs2_devreq_file_operations;
extern void pvfs2_kill_sb(struct super_block *sb);
extern spinlock_t pvfs2_request_list_lock;
extern struct list_head pvfs2_request_list;
extern wait_queue_head_t pvfs2_request_list_waitq;

static int hash_func(void *key, int table_size);
static int hash_compare(void *key, struct qhash_head *link);

/*************************************
 * global variables declared here
 *************************************/

/* the size of the hash tables for ops in progress */
static int hash_table_size = 509;
int debug = 0;
int op_timeout_secs = PVFS2_DEFAULT_OP_TIMEOUT_SECS;

MODULE_LICENSE("GPL");
MODULE_AUTHOR("PVFS2 Development Team");
MODULE_DESCRIPTION("The Linux Kernel VFS interface to PVFS2");
MODULE_PARM_DESC(debug, "debugging level (0 for none, 1 for verbose)");
MODULE_PARM_DESC(hash_table_size, "size of hash table for operations in progress");

#ifdef PVFS2_LINUX_KERNEL_2_4
/*
  for 2.4.x nfs exporting, we need to add fsid=# to the /etc/exports
  file rather than using the FS_REQUIRES_DEV flag
*/
DECLARE_FSTYPE(pvfs2_fs_type, "pvfs2", pvfs2_get_sb, 0);

MODULE_PARM(hash_table_size, "i");
MODULE_PARM(debug, "i");
MODULE_PARM(op_timeout_secs, "i");

#else /* !PVFS2_LINUX_KERNEL_2_4 */

struct file_system_type pvfs2_fs_type =
{
    .name = "pvfs2",
    .get_sb = pvfs2_get_sb,
    .kill_sb = pvfs2_kill_sb,
    .owner = THIS_MODULE,
/*
  NOTE: the FS_REQUIRES_DEV flag is used to honor NFS exporting,
  though we're not really requiring a device.  pvfs2_get_sb is still
  get_sb_nodev and we're using kill_litter_super instead of
  kill_block_super.
*/
    .fs_flags = FS_REQUIRES_DEV,
};

module_param(hash_table_size, int, 0);
module_param(debug, bool, 0);
module_param(op_timeout_secs, int, 0);

#endif /* PVFS2_LINUX_KERNEL_2_4 */

/* the assigned character device major number */
static int pvfs2_dev_major = 0;

/* synchronizes the request device file */
struct semaphore devreq_semaphore;

/*
  blocks non-priority requests from being queued for servicing.  this
  could be used for protecting the request list data structure, but
  for now it's only being used to stall the op addition to the request
  list
*/
struct semaphore request_semaphore;

/* hash table for storing operations waiting for matching downcall */
struct qhash_table *htable_ops_in_progress = NULL;

/* list for queueing upcall operations */
LIST_HEAD(pvfs2_request_list);

/* used to protect the above pvfs2_request_list */
spinlock_t pvfs2_request_list_lock = SPIN_LOCK_UNLOCKED;

/* used for incoming request notification */
DECLARE_WAIT_QUEUE_HEAD(pvfs2_request_list_waitq);


static int __init pvfs2_init(void)
{
    int ret = -1;
    pvfs2_print("pvfs2: pvfs2_init called\n");

    if(debug)
    {
        debug=1; /* normalize any non-zero value to 1 */
        pvfs2_error("pvfs2: verbose debug mode\n");
    }

    if(op_timeout_secs < 0)
    {
        op_timeout_secs = 0;
    }

    /* initialize global book keeping data structures */
    if ((ret = op_cache_initialize()) < 0) {
        goto err;
    }
    if ((ret = dev_req_cache_initialize()) < 0) {
        goto cleanup_op;
    }
    if ((ret = pvfs2_inode_cache_initialize()) < 0) {
        goto cleanup_req;
    }
    if ((ret = kiocb_cache_initialize()) < 0) {
        goto cleanup_inode;
    }

    /* Initialize the ioctl32 subsystem. This could be a noop too */
    if ((ret = pvfs2_ioctl32_init()) < 0)
    {
        pvfs2_error("pvfs2: could not register ioctl32 handlers? %d!\n",
                ret);
        goto cleanup_kiocb;
    }

    /* register pvfs2-req device  */
    pvfs2_dev_major = register_chrdev(0, PVFS2_REQDEVICE_NAME,
                                      &pvfs2_devreq_file_operations);
    if (pvfs2_dev_major < 0)
    {
	pvfs2_error("Failed to register /dev/%s (error %d)\n",
		    PVFS2_REQDEVICE_NAME, pvfs2_dev_major);
        ret = pvfs2_dev_major;
        goto cleanup_ioctl;
    }

    pvfs2_print("*** /dev/%s character device registered ***\n",
		PVFS2_REQDEVICE_NAME);
    pvfs2_print("'mknod /dev/%s c %d 0'.\n", PVFS2_REQDEVICE_NAME,
                pvfs2_dev_major);

    sema_init(&devreq_semaphore, 1);
    sema_init(&request_semaphore, 1);

    htable_ops_in_progress =
	qhash_init(hash_compare, hash_func, hash_table_size);
    if (!htable_ops_in_progress)
    {
	pvfs2_error("Failed to initialize op hashtable");
        ret = -ENOMEM;
        goto cleanup_device;
    }
    if ((ret = fsid_key_table_initialize()) < 0) 
    {
        goto cleanup_progress_table;
    }
    pvfs2_proc_initialize();

    ret = register_filesystem(&pvfs2_fs_type);
    if(ret == 0)
    {
        printk("pvfs2: module version %s loaded\n", PVFS2_VERSION);
        return 0;
    }
    pvfs2_proc_finalize();
    fsid_key_table_finalize();
cleanup_progress_table:
    qhash_finalize(htable_ops_in_progress);
cleanup_device:
    unregister_chrdev(pvfs2_dev_major, PVFS2_REQDEVICE_NAME);
cleanup_ioctl:
    pvfs2_ioctl32_cleanup();
cleanup_kiocb:
    kiocb_cache_finalize();
cleanup_inode:
    pvfs2_inode_cache_finalize();
cleanup_req:
    dev_req_cache_finalize();
cleanup_op:
    op_cache_finalize();
err:
    return ret;
}

static void __exit pvfs2_exit(void)
{
    int i = 0;
    pvfs2_kernel_op_t *cur_op = NULL;
    struct qhash_head *hash_link = NULL;

    pvfs2_print("pvfs2: pvfs2_exit called\n");

    unregister_filesystem(&pvfs2_fs_type);
    pvfs2_proc_finalize();
    fsid_key_table_finalize();

    /* clear out all pending upcall op requests */
    spin_lock(&pvfs2_request_list_lock);
    while (!list_empty(&pvfs2_request_list))
    {
	cur_op = list_entry(pvfs2_request_list.next,
                            pvfs2_kernel_op_t, list);
	list_del(&cur_op->list);
	pvfs2_print("Freeing unhandled upcall request type %d\n",
		    cur_op->upcall.type);
	op_release(cur_op);
    }
    spin_unlock(&pvfs2_request_list_lock);

    for (i = 0; i < htable_ops_in_progress->table_size; i++)
    {
	do
	{
	    hash_link = qhash_search_and_remove_at_index(
                htable_ops_in_progress, i);
	    if (hash_link)
	    {
		cur_op = qhash_entry(hash_link, pvfs2_kernel_op_t, list);
                op_release(cur_op);
	    }
	} while (hash_link);
    }
    qhash_finalize(htable_ops_in_progress);

    if (unregister_chrdev(pvfs2_dev_major, PVFS2_REQDEVICE_NAME) < 0)
    {
	pvfs2_error("Failed to unregister pvfs2 device /dev/%s\n",
		    PVFS2_REQDEVICE_NAME);
    }
    else {
        pvfs2_print("Unregistered pvfs2 device /dev/%s\n",
                    PVFS2_REQDEVICE_NAME);
    }
    /* cleans up all ioctl32 handlers */
    pvfs2_ioctl32_cleanup();
    kiocb_cache_finalize();
    pvfs2_inode_cache_finalize();
    dev_req_cache_finalize();
    op_cache_finalize();

    printk("pvfs2: module version %s unloaded\n", PVFS2_VERSION);
}

static int hash_func(void *key, int table_size)
{
    int tmp = 0;
    uint64_t *real_tag = (uint64_t *)key;
    tmp += (int)(*real_tag);
    tmp = (tmp % table_size);
    return tmp;
}

static int hash_compare(void *key, struct qhash_head *link)
{
    uint64_t *real_tag = (uint64_t *)key;
    pvfs2_kernel_op_t *op = qhash_entry(
        link, pvfs2_kernel_op_t, list);

    return (op->tag == *real_tag);
}

module_init(pvfs2_init);
module_exit(pvfs2_exit);

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
