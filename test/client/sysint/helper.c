/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <client.h>
#include <sys/time.h>
#include "helper.h"

/* make uid, gid and perms passed in later */
PVFS_handle lookup_parent_handle(char *filename, PVFS_fs_id fs_id)
{
    char buf[MAX_PVFS_PATH_LEN] = {0};
    PVFS_sysreq_lookup req_look;
    PVFS_sysresp_lookup resp_look;

    memset(&req_look,0,sizeof(PVFS_sysreq_lookup));
    memset(&resp_look,0,sizeof(PVFS_sysresp_lookup));

    if (PINT_get_base_dir(filename,buf,MAX_PVFS_PATH_LEN))
    {
        if (filename[0] != '/')
        {
            fprintf(stderr,"Invalid dirname (no leading '/')\n");
        }
        fprintf(stderr,"cannot get parent directory of %s\n",filename);
        return (PVFS_handle)0;
    }

    /* retrieve the parent handle */
    req_look.name = buf;
    req_look.fs_id = fs_id;
    req_look.credentials.uid = 100;
    req_look.credentials.gid = 100;
    req_look.credentials.perms = 1877;

    printf("looking up the parent handle of %s for fsid = %d\n",
           buf,req_look.fs_id);

    if (PVFS_sys_lookup(&req_look,&resp_look))
    {
        printf("Lookup failed on %s\n",buf);
        return (PVFS_handle)0;
    }
    return resp_look.pinode_refn.handle;
}
