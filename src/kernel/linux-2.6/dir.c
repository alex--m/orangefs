/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/** \file
 *  \ingroup pvfs2linux
 *
 *  Linux VFS directory operations.
 */

#include "pvfs2-kernel.h"
#include "pvfs2-sysint.h"
#include "pvfs2-internal.h"

extern struct list_head pvfs2_request_list;
extern spinlock_t pvfs2_request_list_lock;
extern wait_queue_head_t pvfs2_request_list_waitq;
extern int debug;

/* shared file/dir operations defined in file.c */
extern int pvfs2_file_open(
    struct inode *inode,
    struct file *file);
extern int pvfs2_file_release(
    struct inode *inode,
    struct file *file);

/** Read directory entries from an instance of an open directory.
 *
 * \param filldir callback function called for each entry read.
 *
 * \retval <0 on error
 * \retval 0  when directory has been completely traversed
 * \retval >0 if we don't call filldir for all entries
 *
 * \note If the filldir call-back returns non-zero, then readdir should
 *       assume that it has had enough, and should return as well.
 */
static int pvfs2_readdir(
    struct file *file,
    void *dirent,
    filldir_t filldir)
{
    int ret = 0, token_set = 0;
    PVFS_ds_position pos = 0;
    ino_t ino = 0;
    struct dentry *dentry = file->f_dentry;
    pvfs2_kernel_op_t *new_op = NULL;
    pvfs2_inode_t *pvfs2_inode = PVFS2_I(dentry->d_inode);

  restart_readdir:

    pos = (PVFS_ds_position)file->f_pos;
    /* are we done? */
    if (pos == PVFS_READDIR_END)
    {
        pvfs2_print("Skipping to graceful termination path since we are done\n");
        pvfs2_inode->directory_version = 0;
        pvfs2_inode->num_readdir_retries =
            PVFS2_NUM_READDIR_RETRIES;
        return 0;
    }

    pvfs2_print("pvfs2_readdir called on %s (pos=%d, "
                "retry=%d, v=%llu)\n", dentry->d_name.name, (int)pos,
                (int)pvfs2_inode->num_readdir_retries,
                llu(pvfs2_inode->directory_version));

    switch (pos)
    {
	/*
	   if we're just starting, populate the "." and ".." entries
	   of the current directory; these always appear
	 */
    case 0:
        token_set = 1;
        if (pvfs2_inode->directory_version == 0)
        {
            ino = dentry->d_inode->i_ino;
            pvfs2_print("calling filldir of . with pos = %d\n", pos);
            if (filldir(dirent, ".", 1, pos, ino, DT_DIR) < 0)
            {
                break;
            }
        }
        file->f_pos++;
        pos++;
    /* drop through */
    case 1:
        token_set = 1;
        if (pvfs2_inode->directory_version == 0)
        {
            ino = parent_ino(dentry);
            pvfs2_print("calling filldir of .. with pos = %d\n", pos);
            if (filldir(dirent, "..", 2, pos, ino, DT_DIR) < 0)
            {
                break;
            }
        }
        file->f_pos++;
        pos++;
	/* drop through */
    default:
	/* handle the normal cases here */
	new_op = op_alloc();
	if (!new_op)
	{
	    return -ENOMEM;
	}
	new_op->upcall.type = PVFS2_VFS_OP_READDIR;

	if (pvfs2_inode && pvfs2_inode->refn.handle &&
            pvfs2_inode->refn.fs_id)
	{
	    new_op->upcall.req.readdir.refn = pvfs2_inode->refn;
	}
	else
	{
	    new_op->upcall.req.readdir.refn.handle =
		pvfs2_ino_to_handle(dentry->d_inode->i_ino);
	    new_op->upcall.req.readdir.refn.fs_id =
		PVFS2_SB(dentry->d_inode->i_sb)->fs_id;
	}
	new_op->upcall.req.readdir.max_dirent_count = MAX_DIRENT_COUNT;

	/* NOTE:
	   the position we send to the readdir upcall is out of
	   sync with file->f_pos since pvfs2 doesn't include the
	   "." and ".." entries that we added above.
        */
	new_op->upcall.req.readdir.token =
            (pos == 2 ? PVFS_READDIR_START : pos);

        ret = service_operation(
            new_op, "pvfs2_readdir", PVFS2_OP_RETRY_COUNT,
            get_interruptible_flag(dentry->d_inode));

	pvfs2_print("Readdir downcall status is %d (dirent_count "
		    "is %d)\n", new_op->downcall.status,
		    new_op->downcall.resp.readdir.dirent_count);

	if (new_op->downcall.status == 0)
	{
	    int i = 0, len = 0;
	    ino_t current_ino = 0;
	    char *current_entry = NULL;

            if (new_op->downcall.resp.readdir.dirent_count == 0)
            {
                goto graceful_termination_path;
            }

            if (pvfs2_inode->directory_version == 0)
            {
                pvfs2_inode->directory_version =
                    new_op->downcall.resp.readdir.directory_version;
            }

            if (pvfs2_inode->num_readdir_retries > -1)
            {
                if (pvfs2_inode->directory_version !=
                    new_op->downcall.resp.readdir.directory_version)
                {
                    pvfs2_print("detected directory change on listing; "
                                "starting over\n");

                    file->f_pos = 0;
                    pvfs2_inode->directory_version =
                        new_op->downcall.resp.readdir.directory_version;

                    op_release(new_op);
                    pvfs2_inode->num_readdir_retries--;
                    goto restart_readdir;
                }
            }
            else
            {
                pvfs2_print("Giving up on readdir retries to avoid "
                            "possible livelock (%d tries attempted)\n",
                            PVFS2_NUM_READDIR_RETRIES);
            }

	    for (i = 0; i < new_op->downcall.resp.readdir.dirent_count; i++)
	    {
                len = new_op->downcall.resp.readdir.d_name_len[i];
                current_entry =
                    &new_op->downcall.resp.readdir.d_name[i][0];
                current_ino =
                    pvfs2_handle_to_ino(
                        new_op->downcall.resp.readdir.refn[i].handle);

                pvfs2_print("calling filldir for %s with len %d, pos %ld\n",
                        current_entry, len, (unsigned long) pos);
                if (filldir(dirent, current_entry, len, pos,
                            current_ino, DT_UNKNOWN) < 0)
                {
                  graceful_termination_path:

                    pvfs2_inode->directory_version = 0;
                    pvfs2_inode->num_readdir_retries =
                        PVFS2_NUM_READDIR_RETRIES;

                    ret = 0;
                    break;
                }
                file->f_pos++;
                pos++;
            }
            /* for the first time around, use the token teturned by the readdir response */
            if (token_set == 1) {
                if (i ==  new_op->downcall.resp.readdir.dirent_count)
                    file->f_pos = new_op->downcall.resp.readdir.token;
                else 
                    file->f_pos = i;
            }
            pvfs2_print("pos = %d, file->f_pos should have been %ld\n", pos, 
                    (unsigned long) file->f_pos);
	}
        else
        {
            pvfs2_print("Failed to readdir (downcall status %d)\n",
                        new_op->downcall.status);
        }

	op_release(new_op);
	break;
    }
    pvfs2_print("pvfs2_readdir about to update_atime %p\n", dentry->d_inode);

#ifdef HAVE_TOUCH_ATIME
    touch_atime(file->f_vfsmnt, dentry);
#else
    update_atime(dentry->d_inode);
#endif


    pvfs2_print("pvfs2_readdir returning %d\n",ret);
    return ret;
}

/** PVFS2 implementation of VFS directory operations */
struct file_operations pvfs2_dir_operations =
{
#ifdef PVFS2_LINUX_KERNEL_2_4
    read : generic_read_dir,
    readdir : pvfs2_readdir,
    open : pvfs2_file_open,
    release : pvfs2_file_release
#else
    .read = generic_read_dir,
    .readdir = pvfs2_readdir,
    .open = pvfs2_file_open,
    .release = pvfs2_file_release
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
