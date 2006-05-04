/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * Changes by Acxiom Corporation to add protocol version to kernel
 * communication, Copyright � Acxiom Corporation, 2005.
 *
 * See COPYING in top-level directory.
 */

#include "pvfs2-kernel.h"
#include "pint-dev-shared.h"
#include "pvfs2-dev-proto.h"
#include "pvfs2-bufmap.h"
#include "pvfs2-internal.h"

/* this file implements the /dev/pvfs2-req device node */
extern kmem_cache_t *dev_req_cache;

extern struct list_head pvfs2_request_list;
extern spinlock_t pvfs2_request_list_lock;
extern wait_queue_head_t pvfs2_request_list_waitq;
extern struct qhash_table *htable_ops_in_progress;
extern struct file_system_type pvfs2_fs_type;
extern struct semaphore devreq_semaphore;
extern struct semaphore request_semaphore;
extern int debug;
extern int op_timeout_secs;

/* defined in super.c */
extern struct list_head pvfs2_superblocks;
extern spinlock_t pvfs2_superblocks_lock;

static int open_access_count = 0;

#define DUMP_DEVICE_ERROR()                                            \
pvfs2_error("*****************************************************\n");\
pvfs2_error("PVFS2 Device Error:  You cannot open the device file ");  \
pvfs2_error("\n/dev/%s more than once.  Please make sure that\nthere " \
            "are no ", PVFS2_REQDEVICE_NAME);                          \
pvfs2_error("instances of a program using this device\ncurrently "     \
            "running. (You must verify this!)\n");                     \
pvfs2_error("For example, you can use the lsof program as follows:\n");\
pvfs2_error("'lsof | grep %s' (run this as root)\n",                   \
            PVFS2_REQDEVICE_NAME);                                     \
pvfs2_error("  open_access_count = %d\n", open_access_count);          \
pvfs2_error("*****************************************************\n")

static int pvfs2_devreq_open(
    struct inode *inode,
    struct file *file)
{
    int ret = -EACCES;

    pvfs2_print("pvfs2_devreq_open: trying to open\n");

    down(&devreq_semaphore);

    if (open_access_count == 0)
    {
        ret = generic_file_open(inode, file);
        if (ret == 0)
        {
#ifdef PVFS2_LINUX_KERNEL_2_4
/*             MOD_INC_USE_COUNT; */
#else
            ret = (try_module_get(pvfs2_fs_type.owner) ? 0 : 1);
#endif
            if (ret == 0)
            {
                open_access_count++;
            }
            else
            {
                pvfs2_panic("PVFS2 Device Error: Cannot obtain reference "
                            "for device file\n");
            }
        }
    }
    else
    {
        DUMP_DEVICE_ERROR();
    }
    up(&devreq_semaphore);

    pvfs2_print("pvfs2_devreq_open: open complete (ret = %d)\n", ret);
    return ret;
}

static ssize_t pvfs2_devreq_read(
    struct file *file,
    char __user *buf,
    size_t count,
    loff_t *offset)
{
    int ret = 0, len = 0;
    pvfs2_kernel_op_t *cur_op = NULL;
    static int32_t magic = PVFS2_DEVREQ_MAGIC;
    int32_t proto_ver = PVFS_KERNEL_PROTO_VERSION;

    if (!(file->f_flags & O_NONBLOCK))
    {
        /* block until we have a request */
        DECLARE_WAITQUEUE(wait_entry, current);

        add_wait_queue(&pvfs2_request_list_waitq, &wait_entry);

        while(1)
        {
            set_current_state(TASK_INTERRUPTIBLE);

            spin_lock(&pvfs2_request_list_lock);
            if (!list_empty(&pvfs2_request_list))
            {
                cur_op = list_entry(
                    pvfs2_request_list.next, pvfs2_kernel_op_t, list);
                list_del(&cur_op->list);
                spin_unlock(&pvfs2_request_list_lock);
                break;
            }
            spin_unlock(&pvfs2_request_list_lock);

            if (!signal_pending(current))
            {
                schedule();
                continue;
            }

            pvfs2_print("*** device read interrupted by signal\n");
            break;
        }

        set_current_state(TASK_RUNNING);
        remove_wait_queue(&pvfs2_request_list_waitq, &wait_entry);
    }
    else
    {
        /* get next op (if any) from top of list */
        spin_lock(&pvfs2_request_list_lock);
        if (!list_empty(&pvfs2_request_list))
        {
            cur_op = list_entry(
                pvfs2_request_list.next, pvfs2_kernel_op_t, list);
            list_del(&cur_op->list);
        }
        spin_unlock(&pvfs2_request_list_lock);
    }

    if (cur_op)
    {
        spin_lock(&cur_op->lock);

        if ((cur_op->op_state == PVFS2_VFS_STATE_INPROGR) ||
            (cur_op->op_state == PVFS2_VFS_STATE_SERVICED))
        {
            pvfs2_panic("WARNING: Current op already queued...skipping\n");
        }
        cur_op->op_state = PVFS2_VFS_STATE_INPROGR;

        /* atomically move the operation to the htable_ops_in_progress */
        qhash_add(htable_ops_in_progress,
                  (void *) &(cur_op->tag), &cur_op->list);

        spin_unlock(&cur_op->lock);

        len = MAX_ALIGNED_DEV_REQ_UPSIZE;
        if ((size_t) len <= count)
        {
            ret = copy_to_user(buf, &proto_ver, sizeof(int32_t));
            if(ret == 0)
            {
                ret = copy_to_user(buf + sizeof(int32_t), &magic, sizeof(int32_t));
                if (ret == 0)
                {
                    ret = copy_to_user(buf + 2*sizeof(int32_t),
                                       &cur_op->tag, sizeof(uint64_t));
                    if (ret == 0)
                    {
                        ret = copy_to_user(
                            buf + 2*sizeof(int32_t) + sizeof(uint64_t),
                            &cur_op->upcall, sizeof(pvfs2_upcall_t));
                    }
                }
            }

            if (ret)
            {
                pvfs2_error("Failed to copy data to user space\n");
                len = -EIO;
            }
        }
        else
        {
            pvfs2_error("Read buffer is too small to copy pvfs2 op\n");
            len = -EIO;
        }
    }
    else if (file->f_flags & O_NONBLOCK)

    {
        /*
          if in non-blocking mode, return EAGAIN since no requests are
          ready yet
        */
        len = -EAGAIN;
    }
    return len;
}

static ssize_t pvfs2_devreq_writev(
    struct file *file,
    const struct iovec *iov,
    unsigned long count,
    loff_t * offset)
{
    pvfs2_kernel_op_t *op = NULL;
    struct qhash_head *hash_link = NULL;
    void *buffer = NULL;
    void *ptr = NULL;
    unsigned long i = 0;
    static int max_downsize = MAX_ALIGNED_DEV_REQ_DOWNSIZE;
    int ret = 0, num_remaining = max_downsize;
    int payload_size = 0;
    int32_t magic = 0;
    int32_t proto_ver = 0;
    uint64_t tag = 0;

    buffer = kmem_cache_alloc(dev_req_cache, PVFS2_CACHE_ALLOC_FLAGS);
    if (!buffer)
    {
	return -ENOMEM;
    }
    ptr = buffer;

    for (i = 0; i < count; i++)
    {
	if (iov[i].iov_len > num_remaining)
	{
	    pvfs2_error("writev error: Freeing buffer and returning\n");
	    kmem_cache_free(dev_req_cache, buffer);
	    return -EMSGSIZE;
	}
	ret = copy_from_user(ptr, iov[i].iov_base, iov[i].iov_len);
        if (ret)
        {
            pvfs2_error("Failed to copy data from user space\n");
	    kmem_cache_free(dev_req_cache, buffer);
            return -EIO;
        }
	num_remaining -= iov[i].iov_len;
	ptr += iov[i].iov_len;
	payload_size += iov[i].iov_len;
    }

    /* these elements are currently 8 byte aligned (8 bytes for (version +  
     * magic) 8 bytes for tag).  If you add another element, either make it 8
     * bytes big, or use get_unaligned when asigning  */
    ptr = buffer;
    proto_ver = *((int32_t *)ptr);
    ptr += sizeof(int32_t);

    magic = *((int32_t *)ptr);
    ptr += sizeof(int32_t);

    tag = *((uint64_t *)ptr);
    ptr += sizeof(uint64_t);

    if (magic != PVFS2_DEVREQ_MAGIC)
    {
        pvfs2_error("Error: Device magic number does not match.\n");
        kmem_cache_free(dev_req_cache, buffer);
        return -EPROTO;
    }
    if (proto_ver != PVFS_KERNEL_PROTO_VERSION)
    {
        pvfs2_error("Error: Device protocol version numbers do not match.\n");
        pvfs2_error("Please check that your pvfs2 module and pvfs2-client versions are consistent.\n");
        kmem_cache_free(dev_req_cache, buffer);
        return -EPROTO;
    }


    /* lookup (and remove) the op based on the tag */
    hash_link = qhash_search_and_remove(htable_ops_in_progress, &(tag));
    if (hash_link)
    {
	op = qhash_entry(hash_link, pvfs2_kernel_op_t, list);
	if (op)
	{
            /* Increase ref count! */
            get_op(op);
	    /* cut off magic and tag from payload size */
	    payload_size -= (2*sizeof(int32_t) + sizeof(uint64_t));
	    if (payload_size <= sizeof(pvfs2_downcall_t))
	    {
		/* copy the passed in downcall into the op */
		memcpy(&op->downcall, ptr, sizeof(pvfs2_downcall_t));
	    }
	    else
	    {
		pvfs2_print("writev: Ignoring %d bytes\n", payload_size);
	    }


            /*
              if this operation is an I/O operation and if it was
              initiated on behalf of a *synchronous* VFS I/O operation,
              only then we need to wait
              for all data to be copied before we can return to avoid
              buffer corruption and races that can pull the buffers
              out from under us.

              Essentially we're synchronizing with other parts of the
              vfs implicitly by not allowing the user space
              application reading/writing this device to return until
              the buffers are done being used.
            */
            if (op->upcall.type == PVFS2_VFS_OP_FILE_IO 
                    && op->upcall.req.io.async_vfs_io == PVFS_VFS_SYNC_IO)
            {
                int timed_out = 0;
                DECLARE_WAITQUEUE(wait_entry, current);
                
                /*
                 * tell the vfs op waiting on a waitqueue 
                 * that this op is done 
                 */
                spin_lock(&op->lock);
                op->op_state = PVFS2_VFS_STATE_SERVICED;
                spin_unlock(&op->lock);

                add_wait_queue_exclusive(
                    &op->io_completion_waitq, &wait_entry);
                wake_up_interruptible(&op->waitq);

                while(1)
                {
                    set_current_state(TASK_INTERRUPTIBLE);

                    spin_lock(&op->lock);
                    if (op->io_completed)
                    {
                        spin_unlock(&op->lock);
                        break;
                    }
                    spin_unlock(&op->lock);

                    if (!signal_pending(current))
                    {
                        int timeout = MSECS_TO_JIFFIES(
                            1000 * op_timeout_secs);
                        if (!schedule_timeout(timeout))
                        {
                            pvfs2_print("*** I/O wait time is up\n");
                            timed_out = 1;
                            break;
                        }
                        continue;
                    }

                    pvfs2_print("*** signal on I/O wait -- aborting\n");
                    break;
                }

                set_current_state(TASK_RUNNING);
                remove_wait_queue(&op->io_completion_waitq, &wait_entry);

                /*
                  NOTE: for I/O operations we handle releasing the op
                  object except in the case of timeout.  the reason we
                  can't free the op in timeout cases is that the op
                  service logic in the vfs retries operations using
                  the same op ptr, thus it can't be freed.
                */
                if (!timed_out)
                {
                    op_release(op);
                }
            }
#ifdef HAVE_AIO_VFS_SUPPORT
            else if (op->upcall.type == PVFS2_VFS_OP_FILE_IO
                    && op->upcall.req.io.async_vfs_io == PVFS_VFS_ASYNC_IO)
            {
                pvfs2_kiocb *x = (pvfs2_kiocb *) op->priv;
                if (x == NULL || x->buffer == NULL 
                        || x->op != op 
                        || x->bytes_to_be_copied <= 0)
                {
                    if (x)
                    {
                        pvfs2_print("WARNING: pvfs2_iocb from op"
                                "has invalid fields! %p, %p(%p), %d\n",
                                x->buffer, x->op, op, (int) x->bytes_to_be_copied);
                    }
                    else
                    {
                        pvfs2_print("WARNING: cannot retrieve the "
                                "pvfs2_iocb pointer from op!\n");
                    }
                    /* Most likely means that it was cancelled! */
                }
                else
                {
                    int bytes_copied;

                    if (op->downcall.status != 0)
                    {
                        ret = pvfs2_normalize_to_errno(
                                op->downcall.status);
                        bytes_copied = ret;
                    }
                    else {
                        bytes_copied = op->downcall.resp.io.amt_complete;
                    }
                    pvfs2_print("[AIO] status of transfer: %d\n", bytes_copied);
                    if (x->rw == PVFS_IO_READ
                            && bytes_copied > 0)
                    {
                        /* try and copy it out to user-space */
                        bytes_copied = pvfs_bufmap_copy_to_user_task(
                                x->tsk,
                                x->buffer,
                                x->buffer_index,
                                bytes_copied);
                    }
                    spin_lock(&op->lock);
                    /* we tell VFS that the op is now serviced! */
                    op->op_state = PVFS2_VFS_STATE_SERVICED;
                    pvfs2_print("Setting state of %p to %d [SERVICED]\n",
                            op, op->op_state);
                    x->bytes_copied = bytes_copied;
                    /* call aio_complete to finish the operation to wake up regular aio waiters */
                    aio_complete(x->kiocb, x->bytes_copied, 0);
                    op->io_completed = 1;
                    /* also wake up any aio cancellers that may be waiting for us to finish the op */
                    wake_up_interruptible(&op->io_completion_waitq);
                    spin_unlock(&op->lock);
                }
                put_op(op);
            }
#endif
            else
            {
                /* tell the vfs op waiting on a waitqueue that this op is done */
                spin_lock(&op->lock);
                op->op_state = PVFS2_VFS_STATE_SERVICED;
                spin_unlock(&op->lock);
                /*
                  for every other operation (i.e. non-I/O), we need to
                  wake up the callers for downcall completion
                  notification
                */
                wake_up_interruptible(&op->waitq);
            }
	}
    }
    else
    {
        /* ignore downcalls that we're not interested in */
	pvfs2_print("WARNING: No one's waiting for tag %llu\n", llu(tag));
    }
    kmem_cache_free(dev_req_cache, buffer);

    return count;
}

/*
  NOTE: gets called when the last reference to this device is dropped.
  Using the open_access_count variable, we enforce a reference count
  on this file so that it can be opened by only one process at a time.
  the devreq_semaphore is used to make sure all i/o has completed
  before we call pvfs_bufmap_finalize, and similar such tricky
  situations
*/
static int pvfs2_devreq_release(
    struct inode *inode,
    struct file *file)
{
    int unmounted = 0;

    pvfs2_print("pvfs2_devreq_release: trying to finalize\n");

    down(&devreq_semaphore);
    pvfs_bufmap_finalize();

    open_access_count--;

#ifdef PVFS2_LINUX_KERNEL_2_4
/*     MOD_DEC_USE_COUNT; */
#else
    module_put(pvfs2_fs_type.owner);
#endif

    /*
      prune dcache here to get rid of entries that may no longer exist
      on device re-open, assuming that the sb has been properly filled
      (may not have been if a mount wasn't attempted)
    */
    spin_lock(&pvfs2_superblocks_lock);
    unmounted = list_empty(&pvfs2_superblocks);
    spin_unlock(&pvfs2_superblocks_lock);

    pvfs2_print("PVFS2 Device Close: Filesystem(s) %s\n",
                (unmounted ? "UNMOUNTED" : "MOUNTED"));

    if (unmounted && inode && inode->i_sb)
    {
        shrink_dcache_sb(inode->i_sb);
    }

    up(&devreq_semaphore);

    pvfs2_print("pvfs2_devreq_release: finalize complete\n");
    return 0;
}

static inline long check_ioctl_command(unsigned int command)
{
    /* Check for valid ioctl codes */
    if (_IOC_TYPE(command) != PVFS_DEV_MAGIC) 
    {
        pvfs2_error("device ioctl magic numbers don't match! "
                "did you rebuild pvfs2-client-core/libpvfs2?"
                "[cmd %x, magic %x != %x]\n",
                command,
                _IOC_TYPE(command),
                PVFS_DEV_MAGIC);
        return -EINVAL;
    }
    /* and valid ioctl commands */
    if (_IOC_NR(command) >= PVFS_DEV_MAXNR || _IOC_NR(command) <= 0) 
    {
        pvfs2_error("Invalid ioctl command number [%d >= %d]\n",
                _IOC_NR(command), PVFS_DEV_MAXNR);
        return -ENOIOCTLCMD;
    }
    return 0;
}

static long dispatch_ioctl_command(unsigned int command, unsigned long arg)
{
    static int32_t magic = PVFS2_DEVREQ_MAGIC;
    static int32_t max_up_size = MAX_ALIGNED_DEV_REQ_UPSIZE;
    static int32_t max_down_size = MAX_ALIGNED_DEV_REQ_DOWNSIZE;
    struct PVFS_dev_map_desc user_desc;

    switch(command)
    {
        case PVFS_DEV_GET_MAGIC:
            return ((put_user(magic, (int32_t __user *)arg) ==
                     -EFAULT) ? -EIO : 0);
        case PVFS_DEV_GET_MAX_UPSIZE:
            return ((put_user(max_up_size, (int32_t __user *)arg) ==
                     -EFAULT) ? -EIO : 0);
        case PVFS_DEV_GET_MAX_DOWNSIZE:
            return ((put_user(max_down_size, (int32_t __user *)arg) ==
                     -EFAULT) ? -EIO : 0);
        case PVFS_DEV_MAP:
        {
            int ret;
            ret = copy_from_user(
                &user_desc, (struct PVFS_dev_map_desc __user *)arg,
                sizeof(struct PVFS_dev_map_desc));
            return (ret ? -EIO : pvfs_bufmap_initialize(&user_desc));
        }
        case PVFS_DEV_REMOUNT_ALL:
        {
            int ret = 0;
            struct list_head *tmp = NULL;
            pvfs2_sb_info_t *pvfs2_sb = NULL;

            pvfs2_print("pvfs2_devreq_ioctl: got PVFS_DEV_REMOUNT_ALL\n");

            /*
              remount all mounted pvfs2 volumes to regain the lost
              dynamic mount tables (if any) -- NOTE: this is done
              without keeping the superblock list locked due to the
              upcall/downcall waiting.  also, the request semaphore is
              used to ensure that no operations will be serviced until
              all of the remounts are serviced (to avoid ops between
              mounts to fail)
            */
            ret = down_interruptible(&request_semaphore);
            if(ret < 0)
            {
                return(ret);
            }
            pvfs2_print("pvfs2_devreq_ioctl: priority remount "
                        "in progress\n");
            list_for_each(tmp, &pvfs2_superblocks) {
                pvfs2_sb = list_entry(tmp, pvfs2_sb_info_t, list);
                if (pvfs2_sb && (pvfs2_sb->sb))
                {
                    pvfs2_print("Remounting SB %p\n", pvfs2_sb);

                    ret = pvfs2_remount(pvfs2_sb->sb, NULL,
                                        pvfs2_sb->data);
                    if (ret)
                    {
                        pvfs2_print("Failed to remount SB %p\n", pvfs2_sb);
                        break;
                    }
                }
            }
            pvfs2_print("pvfs2_devreq_ioctl: priority remount "
                        "complete\n");
            up(&request_semaphore);
            return ret;
        }
        break;
    default:
	return -ENOIOCTLCMD;
    }
    return -ENOIOCTLCMD;
}

static int pvfs2_devreq_ioctl(
    struct inode *inode,
    struct file *file,
    unsigned int command,
    unsigned long arg)
{
    long ret;

    /* Check for properly constructed commands */
    if ((ret = check_ioctl_command(command)) < 0)
    {
        return (int) ret;
    }
    return (int) dispatch_ioctl_command(command, arg);
}

#ifdef CONFIG_COMPAT

#if defined(HAVE_COMPAT_IOCTL_HANDLER) || defined(HAVE_REGISTER_IOCTL32_CONVERSION)

/*  Compat structure for the PVFS_DEV_MAP ioctl */
struct PVFS_dev_map_desc32 
{
    compat_uptr_t ptr;
    int32_t      size;
};

static unsigned long translate_dev_map(
        unsigned long args, long *error)
{
    struct PVFS_dev_map_desc32  __user *p32 = (void __user *) args;
    /* Depending on the architecture, allocate some space on the user-call-stack based on our expected layout */
    struct PVFS_dev_map_desc    __user *p   = compat_alloc_user_space(sizeof(*p));
    u32    addr;

    *error = 0;
    /* get the ptr from the 32 bit user-space */
    if (get_user(addr, &p32->ptr))
        goto err;
    /* try to put that into a 64-bit layout */
    if (put_user(compat_ptr(addr), &p->ptr))
        goto err;
    /* copy the remaining field */
    if (copy_in_user(&p->size, &p32->size, sizeof(int32_t)))
        goto err;
    return (unsigned long) p;
err:
    *error = -EFAULT;
    return 0;
}

#endif

#ifdef HAVE_COMPAT_IOCTL_HANDLER
/*
 * 32 bit user-space apps' ioctl handlers when kernel modules
 * is compiled as a 64 bit one
 */
static long pvfs2_devreq_compat_ioctl(
        struct file *filp, unsigned int cmd, unsigned long args)
{
    long ret;
    unsigned long arg = args;

    /* Check for properly constructed commands */
    if ((ret = check_ioctl_command(cmd)) < 0)
    {
        return ret;
    }
    if (cmd == PVFS_DEV_MAP)
    {
        /* convert the arguments to what we expect internally in kernel space */
        arg = translate_dev_map(args, &ret);
        if (ret < 0)
        {
            pvfs2_error("Could not translate dev map\n");
            return ret;
        }
    }
    /* no other ioctl requires translation */
    return dispatch_ioctl_command(cmd, arg);
}

#endif

#ifdef HAVE_REGISTER_IOCTL32_CONVERSION

static int pvfs2_translate_dev_map(
        unsigned int fd,
        unsigned int cmd,
        unsigned long arg,
        struct   file *file)
{
    long ret;
    unsigned long p;

    /* Copy it as the kernel module expects it */
    p = translate_dev_map(arg, &ret);
    if (ret < 0)
    {
        pvfs2_error("Could not translate dev map structure\n");
        return ret;
    }
    /* p is still a user space address */
    return sys_ioctl(fd, cmd, p);
}

static struct ioctl_trans pvfs2_ioctl32_trans[] = {
    {PVFS_DEV_GET_MAGIC,        NULL},
    {PVFS_DEV_GET_MAX_UPSIZE,   NULL},
    {PVFS_DEV_GET_MAX_DOWNSIZE, NULL},
    {PVFS_DEV_MAP,              pvfs2_translate_dev_map},
    {PVFS_DEV_REMOUNT_ALL,      NULL},
    /* Please add stuff above this line and retain the entry below */
    {0, },
};

/* Must be called on module load */
int pvfs2_ioctl32_init(void)
{
    int i, error;

    for (i = 0;  pvfs2_ioctl32_trans[i].cmd != 0; i++)
    {
        error = register_ioctl32_conversion(
                    pvfs2_ioctl32_trans[i].cmd, pvfs2_ioctl32_trans[i].handler);
        if (error) 
            goto fail;
        pvfs2_print("Registered ioctl32 command %08x with handler %p\n",
                (unsigned int) pvfs2_ioctl32_trans[i].cmd, pvfs2_ioctl32_trans[i].handler);
    }
    return 0;
fail:
    while (--i)
        unregister_ioctl32_conversion(pvfs2_ioctl32_trans[i].cmd);
    return error;
}

/* Must be called on module unload */
void pvfs2_ioctl32_cleanup(void)
{
    int i;

    for (i = 0;  pvfs2_ioctl32_trans[i].cmd != 0; i++)
    {
        pvfs2_print("Deregistered ioctl32 command %08x\n",
               (unsigned int) pvfs2_ioctl32_trans[i].cmd);
        unregister_ioctl32_conversion(pvfs2_ioctl32_trans[i].cmd);
    }
}

#endif /* end HAVE_REGISTER_IOCTL32_CONVERSION */

#endif /* CONFIG_COMPAT */

#if (defined(CONFIG_COMPAT) && !defined(HAVE_REGISTER_IOCTL32_CONVERSION)) || !defined(CONFIG_COMPAT)
int pvfs2_ioctl32_init(void)
{
    return 0;
}

void pvfs2_ioctl32_cleanup(void)
{
    return;
}
#endif

static unsigned int pvfs2_devreq_poll(
    struct file *file,
    struct poll_table_struct *poll_table)
{
    int poll_revent_mask = 0;

    if (open_access_count == 1)
    {
        poll_wait(file, &pvfs2_request_list_waitq, poll_table);

        spin_lock(&pvfs2_request_list_lock);
        if (!list_empty(&pvfs2_request_list))
        {
            poll_revent_mask |= POLL_IN;
        }
        spin_unlock(&pvfs2_request_list_lock);
    }
    return poll_revent_mask;
}

struct file_operations pvfs2_devreq_file_operations =
{
#ifdef PVFS2_LINUX_KERNEL_2_4
    owner: THIS_MODULE,
    read : pvfs2_devreq_read,
    writev : pvfs2_devreq_writev,
    open : pvfs2_devreq_open,
    release : pvfs2_devreq_release,
    ioctl : pvfs2_devreq_ioctl,
    poll : pvfs2_devreq_poll
#else
    .read = pvfs2_devreq_read,
    .writev = pvfs2_devreq_writev,
    .open = pvfs2_devreq_open,
    .release = pvfs2_devreq_release,
    .ioctl = pvfs2_devreq_ioctl,
#ifdef CONFIG_COMPAT
#ifdef HAVE_COMPAT_IOCTL_HANDLER
    .compat_ioctl = pvfs2_devreq_compat_ioctl,
#endif
#endif
    .poll = pvfs2_devreq_poll
#endif
};

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
