/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* types and constants shared between user space and kernel space for
 * device interaction using a common protocol
 */

/************************************
 * valid pvfs2 kernel operation types
 ************************************/
#define PVFS2_VFS_OP_INVALID           0xFF000000
#define PVFS2_VFS_OP_FILE_IO           0xFF000001
#define PVFS2_VFS_OP_LOOKUP            0xFF000002
#define PVFS2_VFS_OP_CREATE            0xFF000003
#define PVFS2_VFS_OP_GETATTR           0xFF000004
#define PVFS2_VFS_OP_REMOVE            0xFF000005
#define PVFS2_VFS_OP_MKDIR             0xFF000006
#define PVFS2_VFS_OP_READDIR           0xFF000007
#define PVFS2_VFS_OP_SETATTR           0xFF000008
#define PVFS2_VFS_OP_SYMLINK           0xFF000009
#define PVFS2_VFS_OP_RENAME            0xFF00000A
#define PVFS2_VFS_OP_STATFS            0xFF00000B
#define PVFS2_VFS_OP_TRUNCATE          0xFF00000C
#define PVFS2_VFS_OP_MMAP_RA_FLUSH     0xFF00000D
#define PVFS2_VFS_OP_FS_MOUNT          0xFF00000E
#define PVFS2_VFS_OP_FS_UMOUNT         0xFF00000F
#define PVFS2_VFS_OP_GETXATTR          0xFF000010
#define PVFS2_VFS_OP_SETXATTR          0xFF000011
#define PVFS2_VFS_OP_LISTXATTR         0xFF000012
#define PVFS2_VFS_OP_REMOVEXATTR       0xFF000013
#define PVFS2_VFS_OP_PARAM             0xFF000014
#define PVFS2_VFS_OP_PERF_COUNT        0xFF000015
#define PVFS2_VFS_OP_CANCEL            0xFF00EE00
#define PVFS2_VFS_OP_FSYNC             0xFF00EE01

/* Misc constants. Please retain them as multiples of 8!
 * Otherwise 32-64 bit interactions will be messed up :)
 */
#define PVFS2_NAME_LEN                 0x00000100
#define MAX_DIRENT_COUNT               0x00000020

#include "pvfs2.h"

#include "upcall.h"
#include "downcall.h"
#include "quickhash.h"

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
