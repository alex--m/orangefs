/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/** \file
 *  \ingroup pvfs2linux
 *
 *  Extended attributes for PVFS2 that handle the trusted prefix.
 */

#include "pvfs2-kernel.h"
#include "pvfs2-bufmap.h"
#include <linux/xattr.h>

int pvfs2_xattr_set_trusted(struct dentry *dentry,
                            const char *name, 
                            const void *buffer, 
                            size_t size,
                            int flags, 
                            int handler_flags)
{
    int internal_flag = 0;

    gossip_debug(GOSSIP_XATTR_DEBUG, "pvfs2_xattr_set_trusted: name %s, buffer_size %zd\n",
            name, size);
    if (strcmp(name, "") == 0)
        return -EINVAL;
    if(!capable(CAP_SYS_ADMIN))
    {
        gossip_err("pvfs2_xattr_set_trusted: operation not permitted\n");
        return -EPERM;
    }
    internal_flag = convert_to_internal_xattr_flags(flags);

    return pvfs2_inode_setxattr(dentry->d_inode, 
        PVFS2_XATTR_NAME_TRUSTED_PREFIX, name, buffer, size, internal_flag);
}

int pvfs2_xattr_get_trusted(struct dentry *dentry,
                            const char *name, 
                            void *buffer, 
                            size_t size,
                            int handler_flags)
{
    gossip_debug(GOSSIP_XATTR_DEBUG, "pvfs2_xattr_get_trusted: name %s, buffer_size %zd\n",
            name, size);
    if (strcmp(name, "") == 0)
        return -EINVAL;
    if(!capable(CAP_SYS_ADMIN))
    {
        gossip_err("pvfs2_xattr_get_trusted: operation not permitted\n");
        return -EPERM;
    }
    return pvfs2_inode_getxattr(dentry->d_inode, 
        PVFS2_XATTR_NAME_TRUSTED_PREFIX, name, buffer, size);
}

struct xattr_handler pvfs2_xattr_trusted_handler = {
    .prefix = PVFS2_XATTR_NAME_TRUSTED_PREFIX, 
    .get    = pvfs2_xattr_get_trusted,
    .set    = pvfs2_xattr_set_trusted,
};

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
