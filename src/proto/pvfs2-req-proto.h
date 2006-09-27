/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */
/* NOTE: if you make any changes to the code contained in this file, please
 * update the PVFS2_PROTO_VERSION accordingly
 */

#ifndef __PVFS2_REQ_PROTO_H
#define __PVFS2_REQ_PROTO_H

#include "pvfs2-config.h"
#include "pvfs2-types.h"
#include "pvfs2-attr.h"
#include "pint-distribution.h"
#include "pvfs2-request.h"
#include "pint-request.h"
#include "pvfs2-mgmt.h"

/* update PVFS2_PROTO_MAJOR on wire protocol changes that break backwards
 * compatibility (such as changing the semantics or protocol fields for an
 * existing request type)
 */
#define PVFS2_PROTO_MAJOR 2
/* update PVFS2_PROTO_MINOR on wire protocol changes that preserve backwards
 * compatibility (such as adding a new request type)
 */
#define PVFS2_PROTO_MINOR 3
#define PVFS2_PROTO_VERSION ((PVFS2_PROTO_MAJOR*1000)+(PVFS2_PROTO_MINOR))

/* we set the maximum possible size of a small I/O packed message as 64K.  This
 * is an upper limit that is used to allocate the request and response encoded
 * buffers, and is independent of the max unexpected message size of the specific
 * BMI module.  All max unexpected message sizes for BMI modules have to be less
 * than this value
 */
#define PINT_SMALL_IO_MAXSIZE (16*1024)

enum PVFS_server_op
{
    PVFS_SERV_INVALID = 0,
    PVFS_SERV_CREATE = 1,
    PVFS_SERV_REMOVE = 2,
    PVFS_SERV_IO = 3,
    PVFS_SERV_GETATTR = 4,
    PVFS_SERV_SETATTR = 5,
    PVFS_SERV_LOOKUP_PATH = 6,
    PVFS_SERV_CRDIRENT = 7,
    PVFS_SERV_RMDIRENT = 8,
    PVFS_SERV_CHDIRENT = 9,
    PVFS_SERV_TRUNCATE = 10,
    PVFS_SERV_MKDIR = 11,
    PVFS_SERV_READDIR = 12,
    PVFS_SERV_GETCONFIG = 13,
    PVFS_SERV_WRITE_COMPLETION = 14,
    PVFS_SERV_FLUSH = 15,
    PVFS_SERV_MGMT_SETPARAM = 16,
    PVFS_SERV_MGMT_NOOP = 17,
    PVFS_SERV_STATFS = 18,
    PVFS_SERV_PERF_UPDATE = 19,  /* not a real protocol request */
    PVFS_SERV_MGMT_PERF_MON = 20,
    PVFS_SERV_MGMT_ITERATE_HANDLES = 21,
    PVFS_SERV_MGMT_DSPACE_INFO_LIST = 22,
    PVFS_SERV_MGMT_EVENT_MON = 23,
    PVFS_SERV_MGMT_REMOVE_OBJECT = 24,
    PVFS_SERV_MGMT_REMOVE_DIRENT = 25,
    PVFS_SERV_MGMT_GET_DIRDATA_HANDLE = 26,
    PVFS_SERV_JOB_TIMER = 27,    /* not a real protocol request */
    PVFS_SERV_PROTO_ERROR = 28,
    PVFS_SERV_GETEATTR = 29,
    PVFS_SERV_SETEATTR = 30,
    PVFS_SERV_DELEATTR = 31,
    PVFS_SERV_LISTEATTR = 32,
    PVFS_SERV_SMALL_IO = 33,
    PVFS_SERV_LOCK = 34,
    /* leave this entry last */
    PVFS_SERV_NUM_OPS
};

/*
 * These ops must always work, even if the server is in admin mode.
 */
#define PVFS_SERV_IS_MGMT_OP(x) \
    ((x) == PVFS_SERV_MGMT_REMOVE_OBJECT \
  || (x) == PVFS_SERV_MGMT_REMOVE_DIRENT)

/* a private internal type */
typedef struct
{
    enum PVFS_server_op type;
    char *type_str;
} __req_resp_type_desc_t;

/******************************************************************/
/* these values define limits on the maximum size of variable length
 * parameters used within the request protocol
 */

/* max size of opaque distribution parameters */
#define PVFS_REQ_LIMIT_DIST_BYTES         1024
/* max size of each configuration file transmitted to clients */
#define PVFS_REQ_LIMIT_CONFIG_FILE_BYTES  16384
/* max size of all path strings */
#define PVFS_REQ_LIMIT_PATH_NAME_BYTES    PVFS_NAME_MAX
/* max size of strings representing a single path element */
#define PVFS_REQ_LIMIT_SEGMENT_BYTES      PVFS_SEGMENT_MAX
/* max total size of I/O request descriptions */
#define PVFS_REQ_LIMIT_IOREQ_BYTES        8192
/* maximum size of distribution name used for the hints */
#define PVFS_REQ_LIMIT_DIST_NAME          128
/* max count of segments allowed per path lookup (note that this governs 
 * the number of handles and attributes returned in lookup_path responses)
 */
#define PVFS_REQ_LIMIT_PATH_SEGMENT_COUNT   40
/* max count of datafiles associated with a logical file */
#define PVFS_REQ_LIMIT_DFILE_COUNT        1024
#define PVFS_REQ_LIMIT_DFILE_COUNT_IS_VALID(dfile_count) \
((dfile_count > 0) && (dfile_count < PVFS_REQ_LIMIT_DFILE_COUNT))
/* max count of directory entries per readdir request */
#define PVFS_REQ_LIMIT_DIRENT_COUNT        512
/* max number of perf metrics returned by mgmt perf mon op */
#define PVFS_REQ_LIMIT_MGMT_PERF_MON_COUNT 16
/* max number of events returned by mgmt event mon op */
#define PVFS_REQ_LIMIT_MGMT_EVENT_MON_COUNT 2048
/* max number of handles returned by any operation using an array of handles */
#define PVFS_REQ_LIMIT_HANDLES_COUNT 1024
/* max number of handles returned by mgmt iterate handles op */
#define PVFS_REQ_LIMIT_MGMT_ITERATE_HANDLES_COUNT \
  PVFS_REQ_LIMIT_HANDLES_COUNT
/* max number of info list items returned by mgmt dspace info list op */
/* max number of dspace info structs returned by mgmt dpsace info op */
#define PVFS_REQ_LIMIT_MGMT_DSPACE_INFO_LIST_COUNT 1024
/* max number of path elements in a lookup_attr response */
#define PVFS_REQ_LIMIT_MAX_PATH_ELEMENTS  40
/* max number of symlinks to resolve before erroring out */
#define PVFS_REQ_LIMIT_MAX_SYMLINK_RESOLUTION_COUNT 8
/* max number of bytes in the key of a key/value pair including null term */
#define PVFS_REQ_LIMIT_KEY_LEN 128
/* max number of bytes in a value of a key/value/pair */
#define PVFS_REQ_LIMIT_VAL_LEN 4096
/* max number of key/value pairs to set or get in a list operation */
#define PVFS_REQ_LIMIT_KEYVAL_LIST 32

/* create *********************************************************/
/* - used to create new metafile and datafile objects */

struct PVFS_servreq_create
{
    PVFS_fs_id fs_id;
    PVFS_ds_type object_type;

    /*
      an array of handle extents that we use to suggest to
      the server from which handle range to allocate for the
      newly created handle(s).  To request a single handle,
      a single extent with first = last should be used.
    */
    PVFS_handle_extent_array handle_extent_array;
};
endecode_fields_3_struct(
    PVFS_servreq_create,
    PVFS_fs_id, fs_id,
    PVFS_ds_type, object_type,
    PVFS_handle_extent_array, handle_extent_array)
#define extra_size_PVFS_servreq_create \
    (PVFS_REQ_LIMIT_HANDLES_COUNT * sizeof(PVFS_handle_extent))

#define PINT_SERVREQ_CREATE_FILL(__req,                \
                                 __creds,              \
                                 __fsid,               \
                                 __objtype,            \
                                 __ext_array)          \
do {                                                   \
    memset(&(__req), 0, sizeof(__req));                \
    (__req).op = PVFS_SERV_CREATE;                     \
    (__req).credentials = (__creds);                   \
    (__req).u.create.fs_id = (__fsid);                 \
    (__req).u.create.object_type = (__objtype);        \
    (__req).u.create.handle_extent_array.extent_count =\
        (__ext_array).extent_count;                    \
    (__req).u.create.handle_extent_array.extent_array =\
        (__ext_array).extent_array;                    \
} while (0)

struct PVFS_servresp_create
{
    PVFS_handle handle;
};
endecode_fields_1_struct(
    PVFS_servresp_create,
    PVFS_handle, handle)

/* remove *****************************************************/
/* - used to remove an existing metafile or datafile object */

struct PVFS_servreq_remove
{
    PVFS_handle handle;
    PVFS_fs_id  fs_id;
};
endecode_fields_2_struct(
    PVFS_servreq_remove,
    PVFS_handle, handle,
    PVFS_fs_id, fs_id)

#define PINT_SERVREQ_REMOVE_FILL(__req,   \
                                 __creds, \
                                 __fsid,  \
                                 __handle)\
do {                                      \
    memset(&(__req), 0, sizeof(__req));   \
    (__req).op = PVFS_SERV_REMOVE;        \
    (__req).credentials = (__creds);      \
    (__req).u.remove.fs_id = (__fsid);    \
    (__req).u.remove.handle = (__handle); \
} while (0)

/* mgmt_remove_object */
/* - used to remove an existing object reference */

struct PVFS_servreq_mgmt_remove_object
{
    PVFS_handle handle;
    PVFS_fs_id fs_id;
};
endecode_fields_2_struct(
    PVFS_servreq_mgmt_remove_object,
    PVFS_handle, handle,
    PVFS_fs_id, fs_id)

#define PINT_SERVREQ_MGMT_REMOVE_OBJECT_FILL(__req,   \
                                             __creds, \
                                             __fsid,  \
                                             __handle)\
do {                                                  \
    memset(&(__req), 0, sizeof(__req));               \
    (__req).op = PVFS_SERV_MGMT_REMOVE_OBJECT;        \
    (__req).credentials = (__creds);                  \
    (__req).u.mgmt_remove_object.fs_id = (__fsid);    \
    (__req).u.mgmt_remove_object.handle = (__handle); \
} while (0)

/* mgmt_remove_dirent */
/* - used to remove an existing dirent under the specified parent ref */

struct PVFS_servreq_mgmt_remove_dirent
{
    PVFS_handle handle;
    PVFS_fs_id fs_id;
    char *entry;
};
endecode_fields_4_struct(
    PVFS_servreq_mgmt_remove_dirent,
    PVFS_handle, handle,
    PVFS_fs_id, fs_id,
    skip4,,
    string, entry)
#define extra_size_PVFS_servreq_mgmt_remove_dirent \
  roundup8(PVFS_REQ_LIMIT_SEGMENT_BYTES+1)

#define PINT_SERVREQ_MGMT_REMOVE_DIRENT_FILL(__req,   \
                                             __creds, \
                                             __fsid,  \
                                             __handle,\
                                             __entry) \
do {                                                  \
    memset(&(__req), 0, sizeof(__req));               \
    (__req).op = PVFS_SERV_MGMT_REMOVE_DIRENT;        \
    (__req).credentials = (__creds);                  \
    (__req).u.mgmt_remove_dirent.fs_id = (__fsid);    \
    (__req).u.mgmt_remove_dirent.handle = (__handle); \
    (__req).u.mgmt_remove_dirent.entry = (__entry);   \
} while (0)

/* mgmt_get_dirdata_handle */
/* - used to retrieve the dirdata handle of the specified parent ref */
struct PVFS_servreq_mgmt_get_dirdata_handle
{
    PVFS_handle handle;
    PVFS_fs_id fs_id;
};
endecode_fields_2_struct(
    PVFS_servreq_mgmt_get_dirdata_handle,
    PVFS_handle, handle,
    PVFS_fs_id, fs_id)

#define PINT_SERVREQ_MGMT_GET_DIRDATA_HANDLE_FILL(__req,   \
                                                  __creds, \
                                                  __fsid,  \
                                                  __handle)\
do {                                                       \
    memset(&(__req), 0, sizeof(__req));                    \
    (__req).op = PVFS_SERV_MGMT_GET_DIRDATA_HANDLE;        \
    (__req).credentials = (__creds);                       \
    (__req).u.mgmt_get_dirdata_handle.fs_id = (__fsid);    \
    (__req).u.mgmt_get_dirdata_handle.handle = (__handle); \
} while (0)

struct PVFS_servresp_mgmt_get_dirdata_handle
{
    PVFS_handle handle;
};
endecode_fields_1_struct(
    PVFS_servresp_mgmt_get_dirdata_handle,
    PVFS_handle, handle)

/* flush
 * - used to flush an object to disk */
struct PVFS_servreq_flush
{
    PVFS_handle handle;
    PVFS_fs_id fs_id;
    int32_t flags;
};
endecode_fields_3_struct(
    PVFS_servreq_flush,
    PVFS_handle, handle,
    PVFS_fs_id, fs_id,
    int32_t, flags)

#define PINT_SERVREQ_FLUSH_FILL(__req,   \
                                __creds, \
                                __fsid,  \
                                __handle)\
do {                                     \
    memset(&(__req), 0, sizeof(__req));  \
    (__req).op = PVFS_SERV_FLUSH;        \
    (__req).credentials = (__creds);     \
    (__req).u.flush.fs_id = (__fsid);    \
    (__req).u.flush.handle = (__handle); \
} while (0)

/* getattr ****************************************************/
/* - retreives attributes based on mask of PVFS_ATTR_XXX values */

struct PVFS_servreq_getattr
{
    PVFS_handle handle; /* handle of target object */
    PVFS_fs_id fs_id;   /* file system */
    uint32_t attrmask;  /* mask of desired attributes */
};
endecode_fields_3_struct(
    PVFS_servreq_getattr,
    PVFS_handle, handle,
    PVFS_fs_id, fs_id,
    uint32_t, attrmask)

#define PINT_SERVREQ_GETATTR_FILL(__req,   \
                                  __creds, \
                                  __fsid,  \
                                  __handle,\
                                  __amask) \
do {                                       \
    memset(&(__req), 0, sizeof(__req));    \
    (__req).op = PVFS_SERV_GETATTR;        \
    (__req).credentials = (__creds);       \
    (__req).u.getattr.fs_id = (__fsid);    \
    (__req).u.getattr.handle = (__handle); \
    (__req).u.getattr.attrmask = (__amask);\
} while (0)

struct PVFS_servresp_getattr
{
    PVFS_object_attr attr;
};
endecode_fields_1_struct(
    PVFS_servresp_getattr,
    PVFS_object_attr, attr)
#define extra_size_PVFS_servresp_getattr \
    extra_size_PVFS_object_attr

/* setattr ****************************************************/
/* - sets attributes specified by mask of PVFS_ATTR_XXX values */

struct PVFS_servreq_setattr
{
    PVFS_handle handle;    /* handle of target object */
    PVFS_fs_id fs_id;      /* file system */
    PVFS_object_attr attr; /* new attributes */
};
endecode_fields_4_struct(
    PVFS_servreq_setattr,
    PVFS_handle, handle,
    PVFS_fs_id, fs_id,
    skip4,,
    PVFS_object_attr, attr)
#define extra_size_PVFS_servreq_setattr \
    extra_size_PVFS_object_attr

#define PINT_SERVREQ_SETATTR_FILL(__req,         \
                                  __creds,       \
                                  __fsid,        \
                                  __handle,      \
                                  __objtype,     \
                                  __attr,        \
                                  __extra_amask) \
do {                                             \
    memset(&(__req), 0, sizeof(__req));          \
    (__req).op = PVFS_SERV_SETATTR;              \
    (__req).credentials = (__creds);             \
    (__req).u.setattr.fs_id = (__fsid);          \
    (__req).u.setattr.handle = (__handle);       \
    (__attr).objtype = (__objtype);              \
    (__attr).mask |= PVFS_ATTR_SYS_TYPE;         \
    PINT_CONVERT_ATTR(&(__req).u.setattr.attr, &(__attr), __extra_amask);\
} while (0)

/* lookup path ************************************************/
/* - looks up as many elements of the specified path as possible */

struct PVFS_servreq_lookup_path
{
    char *path;                  /* path name */
    PVFS_fs_id fs_id;            /* file system */
    PVFS_handle starting_handle; /* handle of path parent */
    /* mask of attribs to return with lookup results */
    uint32_t attrmask;
};
endecode_fields_5_struct(
    PVFS_servreq_lookup_path,
    string, path,
    PVFS_fs_id, fs_id,
    skip4,,
    PVFS_handle, starting_handle,
    uint32_t, attrmask)
#define extra_size_PVFS_servreq_lookup_path \
  roundup8(PVFS_REQ_LIMIT_PATH_NAME_BYTES + 1)

#define PINT_SERVREQ_LOOKUP_PATH_FILL(__req,           \
                                      __creds,         \
                                      __path,          \
                                      __fsid,          \
                                      __handle,        \
                                      __amask)         \
do {                                                   \
    memset(&(__req), 0, sizeof(__req));                \
    (__req).op = PVFS_SERV_LOOKUP_PATH;                \
    (__req).credentials = (__creds);                   \
    (__req).u.lookup_path.path = (__path);             \
    (__req).u.lookup_path.fs_id = (__fsid);            \
    (__req).u.lookup_path.starting_handle = (__handle);\
    (__req).u.lookup_path.attrmask = (__amask);        \
} while (0)

struct PVFS_servresp_lookup_path
{
    /* array of handles for each successfully resolved path segment */
    PVFS_handle *handle_array;            
    /* array of attributes for each path segment (when available) */
    PVFS_object_attr *attr_array;
    uint32_t handle_count; /* # of handles returned */
    uint32_t attr_count;   /* # of attributes returned */
};
endecode_fields_1a_1a_struct(
    PVFS_servresp_lookup_path,
    skip4,,
    uint32_t, handle_count,
    PVFS_handle, handle_array,
    skip4,,
    uint32_t, attr_count,
    PVFS_object_attr, attr_array)
/* this is a big thing that could be either a full path,
* or lots of handles, just use the max io req limit */
#define extra_size_PVFS_servresp_lookup_path \
  (PVFS_REQ_LIMIT_IOREQ_BYTES)

/* mkdir *******************************************************/
/* - makes a new directory object */

struct PVFS_servreq_mkdir
{
    PVFS_fs_id fs_id;      /* file system */
    PVFS_object_attr attr; /* initial attributes */

    /*
      an array of handle extents that we use to suggest to
      the server from which handle range to allocate for the
      newly created handle(s).  To request a single handle,
      a single extent with first = last should be used.
    */
    PVFS_handle_extent_array handle_extent_array;
};
endecode_fields_4_struct(
    PVFS_servreq_mkdir,
    PVFS_fs_id, fs_id,
    skip4,,
    PVFS_object_attr, attr,
    PVFS_handle_extent_array, handle_extent_array)
#define extra_size_PVFS_servreq_mkdir \
    (PVFS_REQ_LIMIT_HANDLES_COUNT * sizeof(PVFS_handle_extent))

#define PINT_SERVREQ_MKDIR_FILL(__req,                 \
                                __creds,               \
                                __fs_id,               \
                                __ext_array,           \
                                __attr)                \
do {                                                   \
    memset(&(__req), 0, sizeof(__req));                \
    (__req).op = PVFS_SERV_MKDIR;                      \
    (__req).credentials = (__creds);                   \
    (__req).u.mkdir.fs_id = __fs_id;                   \
    (__req).u.mkdir.handle_extent_array.extent_count = \
        (__ext_array).extent_count;                    \
    (__req).u.mkdir.handle_extent_array.extent_array = \
        (__ext_array).extent_array;                    \
    (__attr).objtype = PVFS_TYPE_DIRECTORY;            \
    (__attr).mask   |= PVFS_ATTR_SYS_TYPE;             \
    PINT_CONVERT_ATTR(&(__req).u.mkdir.attr, &(__attr), 0);\
} while (0)

struct PVFS_servresp_mkdir
{
    PVFS_handle handle; /* handle of new directory */
};
endecode_fields_1_struct(
    PVFS_servresp_mkdir,
    PVFS_handle, handle)

/* create dirent ***********************************************/
/* - creates a new entry within an existing directory */

struct PVFS_servreq_crdirent
{
    char *name;                /* name of new entry */
    PVFS_handle new_handle;    /* handle of new entry */
    PVFS_handle parent_handle; /* handle of directory */
    PVFS_fs_id fs_id;          /* file system */
};
endecode_fields_4_struct(
    PVFS_servreq_crdirent,
    string, name,
    PVFS_handle, new_handle,
    PVFS_handle, parent_handle,
    PVFS_fs_id, fs_id)
#define extra_size_PVFS_servreq_crdirent \
  roundup8(PVFS_REQ_LIMIT_SEGMENT_BYTES+1)

#define PINT_SERVREQ_CRDIRENT_FILL(__req,           \
                                   __creds,         \
                                   __name,          \
                                   __new_handle,    \
                                   __parent_handle, \
                                   __fs_id)         \
do {                                                \
    memset(&(__req), 0, sizeof(__req));             \
    (__req).op = PVFS_SERV_CRDIRENT;                \
    (__req).credentials = (__creds);                \
    (__req).u.crdirent.name = (__name);             \
    (__req).u.crdirent.new_handle = (__new_handle); \
    (__req).u.crdirent.parent_handle =              \
       (__parent_handle);                           \
    (__req).u.crdirent.fs_id = (__fs_id);           \
} while (0)

/* rmdirent ****************************************************/
/* - removes an existing directory entry */

struct PVFS_servreq_rmdirent
{
    char *entry;               /* name of entry to remove */
    PVFS_handle parent_handle; /* handle of directory */
    PVFS_fs_id fs_id;          /* file system */
};
endecode_fields_3_struct(
    PVFS_servreq_rmdirent,
    string, entry,
    PVFS_handle, parent_handle,
    PVFS_fs_id, fs_id)
#define extra_size_PVFS_servreq_rmdirent \
  roundup8(PVFS_REQ_LIMIT_SEGMENT_BYTES+1)

#define PINT_SERVREQ_RMDIRENT_FILL(__req,         \
                                   __creds,       \
                                   __fsid,        \
                                   __handle,      \
                                   __entry)       \
do {                                              \
    memset(&(__req), 0, sizeof(__req));           \
    (__req).op = PVFS_SERV_RMDIRENT;              \
    (__req).credentials = (__creds);              \
    (__req).u.rmdirent.fs_id = (__fsid);          \
    (__req).u.rmdirent.parent_handle = (__handle);\
    (__req).u.rmdirent.entry = (__entry);         \
} while (0);

struct PVFS_servresp_rmdirent
{
    PVFS_handle entry_handle;   /* handle of removed entry */
};
endecode_fields_1_struct(
    PVFS_servresp_rmdirent,
    PVFS_handle, entry_handle)

/* chdirent ****************************************************/
/* - modifies an existing directory entry on a particular file system */

struct PVFS_servreq_chdirent
{
    char *entry;                   /* name of entry to remove */
    PVFS_handle new_dirent_handle; /* handle of directory */
    PVFS_handle parent_handle;     /* handle of directory */
    PVFS_fs_id fs_id;              /* file system */
};
endecode_fields_4_struct(
    PVFS_servreq_chdirent,
    string, entry,
    PVFS_handle, new_dirent_handle,
    PVFS_handle, parent_handle,
    PVFS_fs_id, fs_id)
#define extra_size_PVFS_servreq_chdirent \
  roundup8(PVFS_REQ_LIMIT_SEGMENT_BYTES+1)

#define PINT_SERVREQ_CHDIRENT_FILL(__req,          \
                                   __creds,        \
                                   __fsid,         \
                                   __parent_handle,\
                                   __new_dirent,   \
                                   __entry)        \
do {                                               \
    memset(&(__req), 0, sizeof(__req));            \
    (__req).op = PVFS_SERV_CHDIRENT;               \
    (__req).credentials = (__creds);               \
    (__req).u.chdirent.fs_id = (__fsid);           \
    (__req).u.chdirent.parent_handle =             \
        (__parent_handle);                         \
    (__req).u.chdirent.new_dirent_handle =         \
        (__new_dirent);                            \
    (__req).u.chdirent.entry = (__entry);          \
} while (0);

struct PVFS_servresp_chdirent
{
    PVFS_handle old_dirent_handle;
};
endecode_fields_1_struct(
    PVFS_servresp_chdirent,
    PVFS_handle, old_dirent_handle)

/* readdir *****************************************************/
/* - reads entries from a directory */

struct PVFS_servreq_readdir
{
    PVFS_handle handle;     /* handle of dir object */
    PVFS_fs_id fs_id;       /* file system */
    PVFS_ds_position token; /* dir offset */
    uint32_t dirent_count;  /* desired # of entries */
};
endecode_fields_4_struct(
    PVFS_servreq_readdir,
    PVFS_handle, handle,
    PVFS_fs_id, fs_id,
    PVFS_ds_position, token,
    uint32_t, dirent_count)

#define PINT_SERVREQ_READDIR_FILL(__req,              \
                                  __creds,            \
                                  __fsid,             \
                                  __handle,           \
                                  __token,            \
                                  __dirent_count)     \
do {                                                  \
    memset(&(__req), 0, sizeof(__req));               \
    (__req).op = PVFS_SERV_READDIR;                   \
    (__req).credentials = (__creds);                  \
    (__req).u.readdir.fs_id = (__fsid);               \
    (__req).u.readdir.handle = (__handle);            \
    (__req).u.readdir.token = (__token);              \
    (__req).u.readdir.dirent_count = (__dirent_count);\
} while (0);

struct PVFS_servresp_readdir
{
    PVFS_ds_position token;  /* new dir offset */
    /* array of directory entries */
    PVFS_dirent *dirent_array;
    uint32_t dirent_count;   /* # of entries retrieved */
    uint64_t directory_version;
};
endecode_fields_4a_struct(
    PVFS_servresp_readdir,
    PVFS_ds_position, token,
    skip4,,
    uint64_t, directory_version,
    skip4,,
    uint32_t, dirent_count,
    PVFS_dirent, dirent_array)
#define extra_size_PVFS_servresp_readdir \
  roundup8(PVFS_REQ_LIMIT_DIRENT_COUNT * (PVFS_NAME_MAX + 1 + 8))

/* getconfig ***************************************************/
/* - retrieves initial configuration information from server */

#define PINT_SERVREQ_GETCONFIG_FILL(__req, __creds)\
do {                                               \
    memset(&(__req), 0, sizeof(__req));            \
    (__req).op = PVFS_SERV_GETCONFIG;              \
    (__req).credentials = (__creds);               \
} while (0);

struct PVFS_servresp_getconfig
{
    char *fs_config_buf;
    uint32_t fs_config_buf_size;
    char *server_config_buf;
    uint32_t server_config_buf_size;
};
endecode_fields_4_struct(
    PVFS_servresp_getconfig,
    uint32_t, fs_config_buf_size,
    uint32_t, server_config_buf_size,
    string, fs_config_buf,
    string, server_config_buf)
#define extra_size_PVFS_servresp_getconfig \
    (2 * PVFS_REQ_LIMIT_CONFIG_FILE_BYTES)

/* truncate ****************************************************/
/* - resizes an existing datafile */

struct PVFS_servreq_truncate
{
    PVFS_handle handle; /* handle of obj to resize */
    PVFS_fs_id fs_id;   /* file system */
    PVFS_size size;     /* new size */
    int32_t flags;      /* future use */

};
endecode_fields_5_struct(
    PVFS_servreq_truncate,
    PVFS_handle, handle,
    PVFS_fs_id, fs_id,
    skip4,,
    PVFS_size, size,
    int32_t, flags)
#define PINT_SERVREQ_TRUNCATE_FILL(__req,  \
                                __creds,   \
                                __fsid,    \
                                __size,    \
                                __handle)  \
do {                                       \
    memset(&(__req), 0, sizeof(__req));    \
    (__req).op = PVFS_SERV_TRUNCATE;       \
    (__req).credentials = (__creds);       \
    (__req).u.truncate.fs_id = (__fsid);   \
    (__req).u.truncate.size = (__size);    \
    (__req).u.truncate.handle = (__handle);\
} while (0)

/* statfs ****************************************************/
/* - retrieves statistics for a particular file system */

struct PVFS_servreq_statfs
{
    PVFS_fs_id fs_id;  /* file system */
};
endecode_fields_1_struct(
    PVFS_servreq_statfs,
    PVFS_fs_id, fs_id)

#define PINT_SERVREQ_STATFS_FILL(__req, __creds, __fsid)\
do {                                                    \
    memset(&(__req), 0, sizeof(__req));                 \
    (__req).op = PVFS_SERV_STATFS;                      \
    (__req).credentials = (__creds);                    \
    (__req).u.statfs.fs_id = (__fsid);                  \
} while (0)

struct PVFS_servresp_statfs
{
    PVFS_statfs stat;
};
endecode_fields_1_struct(
    PVFS_servresp_statfs,
    PVFS_statfs, stat)

/* lock req - Makes a request to I/O servers to lock/unlock file
 * regions
 */
struct PVFS_servreq_lock
{
    PVFS_handle handle;        /* target datafile */
    PVFS_fs_id fs_id;          /* file system */
    /* type of I/O operation to perform */
    enum PVFS_io_type io_type; /* enum defined in pvfs2-types.h */

    /* type of lock operation to perform */
    enum PVFS_lock_type lock_type; /* enum defined in pvfs2-types.h */

    /* relative number of this I/O server in distribution */
    uint32_t server_nr;
    /* total number of I/O servers involved in distribution */
    uint32_t server_ct;

    /* distribution */
    PINT_dist *io_dist;
    /* file datatype */
    struct PINT_Request * file_req;
    /* offset into file datatype */
    PVFS_offset file_req_offset;
    /* aggregate size of data to transfer */
    PVFS_size aggregate_size;
};
#ifdef __PINT_REQPROTO_ENCODE_FUNCS_C
#define encode_PVFS_servreq_lock(pptr,x) do { \
    encode_PVFS_handle(pptr, &(x)->handle); \
    encode_PVFS_fs_id(pptr, &(x)->fs_id); \
    encode_skip4(pptr,); \
    encode_enum(pptr, &(x)->io_type); \
    encode_enum(pptr, &(x)->lock_type); \
    encode_uint32_t(pptr, &(x)->server_nr); \
    encode_uint32_t(pptr, &(x)->server_ct); \
    encode_PINT_dist(pptr, &(x)->io_dist); \
    encode_PINT_Request(pptr, &(x)->file_req); \
    encode_PVFS_offset(pptr, &(x)->file_req_offset); \
    encode_PVFS_size(pptr, &(x)->aggregate_size); \
} while (0)
#define decode_PVFS_servreq_lock(pptr,x) do { \
    decode_PVFS_handle(pptr, &(x)->handle); \
    decode_PVFS_fs_id(pptr, &(x)->fs_id); \
    decode_skip4(pptr,); \
    decode_enum(pptr, &(x)->io_type); \
    decode_enum(pptr, &(x)->lock_type); \
    decode_uint32_t(pptr, &(x)->server_nr); \
    decode_uint32_t(pptr, &(x)->server_ct); \
    decode_PINT_dist(pptr, &(x)->io_dist); \
    decode_PINT_Request(pptr, &(x)->file_req); \
    PINT_request_decode((x)->file_req); /* unpacks the pointers */ \
    decode_PVFS_offset(pptr, &(x)->file_req_offset); \
    decode_PVFS_size(pptr, &(x)->aggregate_size); \
} while (0)
/* could be huge, limit to max ioreq size beyond struct itself */
#define extra_size_PVFS_servreq_lock roundup8(PVFS_REQ_LIMIT_PATH_NAME_BYTES) \
  + roundup8(PVFS_REQ_LIMIT_PINT_REQUEST_NUM * sizeof(PINT_Request))
#endif

#define PINT_SERVREQ_LOCK_FILL(__req,                   \
                               __creds,                 \
                               __fsid,                  \
                               __handle,                \
                               __io_type,               \
                               __lock_type,             \
                               __datafile_nr,           \
                               __datafile_ct,           \
                               __io_dist,               \
                               __file_req,              \
                               __file_req_off,          \
                               __aggregate_size)        \
do {                                                    \
    memset(&(__req), 0, sizeof(__req));                 \
    (__req).op                 = PVFS_SERV_LOCK;        \
    (__req).credentials        = (__creds);             \
    (__req).u.lock.fs_id         = (__fsid);            \
    (__req).u.lock.handle        = (__handle);          \
    (__req).u.lock.io_type       = (__io_type);         \
    (__req).u.lock.lock_type       = (__lock_type);     \
    (__req).u.lock.server_nr       = (__datafile_nr);   \
    (__req).u.lock.server_ct     = (__datafile_ct);     \
    (__req).u.lock.io_dist       = (__io_dist);         \
    (__req).u.lock.file_req        = (__file_req);      \
    (__req).u.lock.file_req_offset = (__file_req_off);  \
    (__req).u.lock.aggregate_size  = (__aggregate_size);\
} while (0)

/* Lock server resp is the size of the bstream and the lock_id.  */

struct PVFS_servresp_lock
{
    PVFS_id_gen_t lock_id; /* unique lock request ID */
    enum PVFS_lock_type lock_type;
    PVFS_size bstream_size;  /* size of datafile */
    PVFS_size granted_bytes; /* amount of locks immediately granted */
};

endecode_fields_4_struct(
    PVFS_servresp_lock,
    PVFS_id_gen_t, lock_id,
    enum, lock_type,
    PVFS_size, bstream_size,
    PVFS_size, granted_bytes)

/* io **********************************************************/
/* - performs a read or write operation */

struct PVFS_servreq_io
{
    PVFS_handle handle;        /* target datafile */
    PVFS_fs_id fs_id;          /* file system */
    /* type of I/O operation to perform */
    enum PVFS_io_type io_type; /* enum defined in pvfs2-types.h */

    /* type of flow protocol to use for I/O transfer */
    enum PVFS_flowproto_type flow_type;

    /* relative number of this I/O server in distribution */
    uint32_t server_nr;
    /* total number of I/O servers involved in distribution */
    uint32_t server_ct;

    /* distribution */
    PINT_dist *io_dist;
    /* file datatype */
    struct PINT_Request * file_req;
    /* offset into file datatype */
    PVFS_offset file_req_offset;
    /* aggregate size of data to transfer */
    PVFS_size aggregate_size;
};
#ifdef __PINT_REQPROTO_ENCODE_FUNCS_C
#define encode_PVFS_servreq_io(pptr,x) do { \
    encode_PVFS_handle(pptr, &(x)->handle); \
    encode_PVFS_fs_id(pptr, &(x)->fs_id); \
    encode_skip4(pptr,); \
    encode_enum(pptr, &(x)->io_type); \
    encode_enum(pptr, &(x)->flow_type); \
    encode_uint32_t(pptr, &(x)->server_nr); \
    encode_uint32_t(pptr, &(x)->server_ct); \
    encode_PINT_dist(pptr, &(x)->io_dist); \
    encode_PINT_Request(pptr, &(x)->file_req); \
    encode_PVFS_offset(pptr, &(x)->file_req_offset); \
    encode_PVFS_size(pptr, &(x)->aggregate_size); \
} while (0)
#define decode_PVFS_servreq_io(pptr,x) do { \
    decode_PVFS_handle(pptr, &(x)->handle); \
    decode_PVFS_fs_id(pptr, &(x)->fs_id); \
    decode_skip4(pptr,); \
    decode_enum(pptr, &(x)->io_type); \
    decode_enum(pptr, &(x)->flow_type); \
    decode_uint32_t(pptr, &(x)->server_nr); \
    decode_uint32_t(pptr, &(x)->server_ct); \
    decode_PINT_dist(pptr, &(x)->io_dist); \
    decode_PINT_Request(pptr, &(x)->file_req); \
    PINT_request_decode((x)->file_req); /* unpacks the pointers */ \
    decode_PVFS_offset(pptr, &(x)->file_req_offset); \
    decode_PVFS_size(pptr, &(x)->aggregate_size); \
} while (0)
/* could be huge, limit to max ioreq size beyond struct itself */
#define extra_size_PVFS_servreq_io roundup8(PVFS_REQ_LIMIT_PATH_NAME_BYTES) \
  + roundup8(PVFS_REQ_LIMIT_PINT_REQUEST_NUM * sizeof(PINT_Request))
#endif

#define PINT_SERVREQ_IO_FILL(__req,                   \
                             __creds,                 \
                             __fsid,                  \
                             __handle,                \
                             __io_type,               \
                             __flow_type,             \
                             __datafile_nr,           \
                             __datafile_ct,           \
                             __io_dist,               \
                             __file_req,              \
                             __file_req_off,          \
                             __aggregate_size)        \
do {                                                  \
    memset(&(__req), 0, sizeof(__req));               \
    (__req).op                 = PVFS_SERV_IO;        \
    (__req).credentials        = (__creds);           \
    (__req).u.io.fs_id         = (__fsid);            \
    (__req).u.io.handle        = (__handle);          \
    (__req).u.io.io_type       = (__io_type);         \
    (__req).u.io.flow_type     = (__flow_type);       \
    (__req).u.io.server_nr       = (__datafile_nr);   \
    (__req).u.io.server_ct     = (__datafile_ct);     \
    (__req).u.io.io_dist       = (__io_dist);         \
    (__req).u.io.file_req        = (__file_req);      \
    (__req).u.io.file_req_offset = (__file_req_off);  \
    (__req).u.io.aggregate_size  = (__aggregate_size);\
} while (0)                             

struct PVFS_servresp_io
{
    PVFS_size bstream_size;  /* size of datafile */
};
endecode_fields_1_struct(
    PVFS_servresp_io,
    PVFS_size, bstream_size)

/* write operations require a second response to announce completion */
struct PVFS_servresp_write_completion
{
    PVFS_size total_completed; /* amount of data transferred */
};
endecode_fields_1_struct(
    PVFS_servresp_write_completion,
    PVFS_size, total_completed)

#define SMALL_IO_MAX_SEGMENTS 64

struct PVFS_servreq_small_io
{
    PVFS_handle handle;
    PVFS_fs_id fs_id;
    enum PVFS_io_type io_type;

    uint32_t server_nr;
    uint32_t server_ct;

    PINT_dist * dist;
    struct PINT_Request * file_req;
    PVFS_offset file_req_offset;
    PVFS_size aggregate_size;

    /* these are used for writes to map the regions of the memory buffer
     * to the contiguous encoded message.  They don't get encoded.
     */
    int segments;
    PVFS_offset offsets[SMALL_IO_MAX_SEGMENTS];
    PVFS_size sizes[SMALL_IO_MAX_SEGMENTS];

    PVFS_size total_bytes; /* changed from int32_t */
    char * buffer;
};

#ifdef __PINT_REQPROTO_ENCODE_FUNCS_C
#define encode_PVFS_servreq_small_io(pptr,x) do { \
    encode_PVFS_handle(pptr, &(x)->handle); \
    encode_PVFS_fs_id(pptr, &(x)->fs_id); \
    encode_enum(pptr, &(x)->io_type); \
    encode_uint32_t(pptr, &(x)->server_nr); \
    encode_uint32_t(pptr, &(x)->server_ct); \
    encode_PINT_dist(pptr, &(x)->dist); \
    encode_PINT_Request(pptr, &(x)->file_req); \
    encode_PVFS_offset(pptr, &(x)->file_req_offset); \
    encode_PVFS_size(pptr, &(x)->aggregate_size); \
    encode_PVFS_size(pptr, &(x)->total_bytes); \
    encode_skip4(pptr,); \
    if ((x)->io_type == PVFS_IO_WRITE) \
    { \
        int i = 0; \
        for(; i < (x)->segments; ++i) \
        { \
            memcpy((*pptr), \
                   (char *)(x)->buffer + ((x)->offsets[i]), \
                   (x)->sizes[i]); \
            (*pptr) += (x)->sizes[i]; \
        } \
    } \
} while (0)

#define decode_PVFS_servreq_small_io(pptr,x) do { \
    decode_PVFS_handle(pptr, &(x)->handle); \
    decode_PVFS_fs_id(pptr, &(x)->fs_id); \
    decode_enum(pptr, &(x)->io_type); \
    decode_uint32_t(pptr, &(x)->server_nr); \
    decode_uint32_t(pptr, &(x)->server_ct); \
    decode_PINT_dist(pptr, &(x)->dist); \
    decode_PINT_Request(pptr, &(x)->file_req); \
    PINT_request_decode((x)->file_req); /* unpacks the pointers */ \
    decode_PVFS_offset(pptr, &(x)->file_req_offset); \
    decode_PVFS_size(pptr, &(x)->aggregate_size); \
    decode_PVFS_size(pptr, &(x)->total_bytes); \
    decode_skip4(pptr,); \
    if ((x)->io_type == PVFS_IO_WRITE) \
    { \
        /* instead of copying the message we just set the pointer, since \
         * we know it will not be freed unil the small io state machine \
         * has completed. \
         */ \
        (x)->buffer = (*pptr); \
        (*pptr) += (x)->total_bytes; \
    } \
} while (0)
#endif

#define extra_size_PVFS_servreq_small_io PINT_SMALL_IO_MAXSIZE

/* could be huge, limit to max ioreq size beyond struct itself */
#define PINT_SERVREQ_SMALL_IO_FILL(__req,                                \
                                   __creds,                              \
                                   __fsid,                               \
                                   __handle,                             \
                                   __io_type,                            \
                                   __dfile_nr,                           \
                                   __dfile_ct,                           \
                                   __dist,                               \
                                   __filereq,                            \
                                   __filereq_offset,                     \
                                   __segments,                           \
                                   __memreq_size)                        \
do {                                                                     \
    int _sio_i;                                                          \
    (__req).op                                = PVFS_SERV_SMALL_IO;      \
    (__req).credentials                       = (__creds);               \
    (__req).u.small_io.fs_id                  = (__fsid);                \
    (__req).u.small_io.handle                 = (__handle);              \
    (__req).u.small_io.io_type                = (__io_type);             \
    (__req).u.small_io.server_nr              = (__dfile_nr);            \
    (__req).u.small_io.server_ct              = (__dfile_ct);            \
    (__req).u.small_io.dist                   = (__dist);                \
    (__req).u.small_io.file_req               = (__filereq);             \
    (__req).u.small_io.file_req_offset        = (__filereq_offset);      \
    (__req).u.small_io.aggregate_size         = (__memreq_size);         \
    (__req).u.small_io.segments               = (__segments);            \
    (__req).u.small_io.total_bytes            = 0;                       \
    for(_sio_i = 0; _sio_i < (__segments); ++_sio_i)                     \
    {                                                                    \
        (__req).u.small_io.total_bytes +=                                \
            (__req).u.small_io.sizes[_sio_i];                            \
    }                                                                    \
} while(0)

struct PVFS_servresp_small_io
{
    enum PVFS_io_type io_type;

    /* the io state machine needs the total bstream size to calculate
     * the correct return size
     */
    PVFS_size bstream_size;

    /* for writes, this is the amount written.  
     * for reads, this is the number of bytes read */
    PVFS_size result_size; 
    char * buffer;
};

#ifdef __PINT_REQPROTO_ENCODE_FUNCS_C
#define encode_PVFS_servresp_small_io(pptr,x) \
    do { \
        encode_enum(pptr, &(x)->io_type); \
        encode_skip4(pptr,); \
        encode_PVFS_size(pptr, &(x)->bstream_size); \
        encode_PVFS_size(pptr, &(x)->result_size); \
        if((x)->io_type == PVFS_IO_READ && (x)->buffer) \
        { \
            memcpy((*pptr), (x)->buffer, (x)->result_size); \
            (*pptr) += (x)->result_size; \
        } \
    } while(0)

#define decode_PVFS_servresp_small_io(pptr,x) \
    do { \
        decode_enum(pptr, &(x)->io_type); \
        decode_skip4(pptr,); \
        decode_PVFS_size(pptr, &(x)->bstream_size); \
        decode_PVFS_size(pptr, &(x)->result_size); \
        if((x)->io_type == PVFS_IO_READ) \
        { \
            (x)->buffer = (*pptr); \
            (*pptr) += (x)->result_size; \
        } \
    } while(0)
#endif

#define extra_size_PVFS_servresp_small_io PINT_SMALL_IO_MAXSIZE
/* mgmt_setparam ****************************************************/
/* - management operation for setting runtime parameters */

struct PVFS_servreq_mgmt_setparam
{
    PVFS_fs_id fs_id;             /* file system */
    enum PVFS_server_param param; /* parameter to set */
    uint64_t value;               /* parameter value */
};
endecode_fields_3_struct(
    PVFS_servreq_mgmt_setparam,
    PVFS_fs_id, fs_id,
    enum, param,
    uint64_t, value)

#define PINT_SERVREQ_MGMT_SETPARAM_FILL(__req,  \
                                        __creds,\
                                        __fsid, \
                                        __param,\
                                        __value)\
do {                                            \
    memset(&(__req), 0, sizeof(__req));         \
    (__req).op = PVFS_SERV_MGMT_SETPARAM;       \
    (__req).credentials = (__creds);            \
    (__req).u.mgmt_setparam.fs_id = (__fsid);   \
    (__req).u.mgmt_setparam.param = (__param);  \
    (__req).u.mgmt_setparam.value = (__value);  \
} while (0)

struct PVFS_servresp_mgmt_setparam
{
    uint64_t old_value;
};
endecode_fields_1_struct(
    PVFS_servresp_mgmt_setparam,
    uint64_t, old_value)

/* mgmt_noop ********************************************************/
/* - does nothing except contact a server to see if it is responding
 * to requests
 */

#define PINT_SERVREQ_MGMT_NOOP_FILL(__req, __creds)\
do {                                               \
    memset(&(__req), 0, sizeof(__req));            \
    (__req).op = PVFS_SERV_MGMT_NOOP;              \
    (__req).credentials = (__creds);               \
} while (0)


/* mgmt_perf_mon ****************************************************/
/* retrieves performance statistics from server */

struct PVFS_servreq_mgmt_perf_mon
{
    uint32_t next_id;  /* next time stamp id we want to retrieve */
    uint32_t count;    /* how many measurements we want */
};
endecode_fields_2_struct(
    PVFS_servreq_mgmt_perf_mon,
    uint32_t, next_id,
    uint32_t, count)

#define PINT_SERVREQ_MGMT_PERF_MON_FILL(__req,    \
                                        __creds,  \
                                        __next_id,\
                                        __count)  \
do {                                              \
    memset(&(__req), 0, sizeof(__req));           \
    (__req).op = PVFS_SERV_MGMT_PERF_MON;         \
    (__req).credentials = (__creds);              \
    (__req).u.mgmt_perf_mon.next_id = (__next_id);\
    (__req).u.mgmt_perf_mon.count = (__count);    \
} while (0)

struct PVFS_servresp_mgmt_perf_mon
{
    struct PVFS_mgmt_perf_stat* perf_array; /* array of statistics */
    uint32_t perf_array_count;              /* size of above array */
    /* next id to pick up from this point */
    uint32_t suggested_next_id;
    uint64_t end_time_ms;  /* end time for final array entry */
    uint64_t cur_time_ms;  /* current time according to svr */
};
endecode_fields_5a_struct(
    PVFS_servresp_mgmt_perf_mon,
    uint32_t, suggested_next_id,
    skip4,,
    uint64_t, end_time_ms,
    uint64_t, cur_time_ms,
    skip4,,
    uint32_t, perf_array_count,
    PVFS_mgmt_perf_stat, perf_array)
#define extra_size_PVFS_servresp_mgmt_perf_mon \
    (PVFS_REQ_LIMIT_IOREQ_BYTES)

/* mgmt_iterate_handles ***************************************/
/* iterates through handles stored on server */

struct PVFS_servreq_mgmt_iterate_handles
{
    PVFS_fs_id fs_id;
    int32_t handle_count;
    PVFS_ds_position position;
};
endecode_fields_3_struct(
    PVFS_servreq_mgmt_iterate_handles,
    PVFS_fs_id, fs_id,
    int32_t, handle_count,
    PVFS_ds_position, position)

#define PINT_SERVREQ_MGMT_ITERATE_HANDLES_FILL(__req,              \
                                        __creds,                   \
                                        __fs_id,                   \
                                        __handle_count,            \
                                        __position)                \
do {                                                               \
    memset(&(__req), 0, sizeof(__req));                            \
    (__req).op = PVFS_SERV_MGMT_ITERATE_HANDLES;                   \
    (__req).credentials = (__creds);                               \
    (__req).u.mgmt_iterate_handles.fs_id = (__fs_id);              \
    (__req).u.mgmt_iterate_handles.handle_count = (__handle_count);\
    (__req).u.mgmt_iterate_handles.position = (__position);        \
} while (0)

struct PVFS_servresp_mgmt_iterate_handles
{
    PVFS_ds_position position;
    PVFS_handle *handle_array;
    int handle_count;
};
endecode_fields_1a_struct(
    PVFS_servresp_mgmt_iterate_handles,
    PVFS_ds_position, position,
    int32_t, handle_count,
    PVFS_handle, handle_array)
#define extra_size_PVFS_servresp_mgmt_iterate_handles \
  (PVFS_REQ_LIMIT_HANDLES_COUNT * sizeof(PVFS_handle))

/* mgmt_dspace_info_list **************************************/
/* - returns low level dspace information for a list of handles */

struct PVFS_servreq_mgmt_dspace_info_list
{
    PVFS_fs_id fs_id;
    PVFS_handle* handle_array;
    int32_t handle_count;
};
endecode_fields_1a_struct(
    PVFS_servreq_mgmt_dspace_info_list,
    PVFS_fs_id, fs_id,
    int32_t, handle_count,
    PVFS_handle, handle_array)
#define extra_size_PVFS_servreq_mgmt_dspace_info_list \
  (PVFS_REQ_LIMIT_HANDLES_COUNT * sizeof(PVFS_handle))

#define PINT_SERVREQ_MGMT_DSPACE_INFO_LIST(__req,                   \
                                        __creds,                    \
                                        __fs_id,                    \
                                        __handle_array,             \
                                        __handle_count)             \
do {                                                                \
    memset(&(__req), 0, sizeof(__req));                             \
    (__req).op = PVFS_SERV_MGMT_DSPACE_INFO_LIST;                   \
    (__req).credentials = (__creds);                                \
    (__req).u.mgmt_dspace_info_list.fs_id = (__fs_id);              \
    (__req).u.mgmt_dspace_info_list.handle_array = (__handle_array);\
    (__req).u.mgmt_dspace_info_list.handle_count = (__handle_count);\
} while (0)

struct PVFS_servresp_mgmt_dspace_info_list
{
    struct PVFS_mgmt_dspace_info *dspace_info_array;
    int32_t dspace_info_count;
};
endecode_fields_1a_struct(
    PVFS_servresp_mgmt_dspace_info_list,
    skip4,,
    int32_t, dspace_info_count,
    PVFS_mgmt_dspace_info, dspace_info_array)
#define extra_size_PVFS_servresp_mgmt_dspace_info_list \
   (PVFS_REQ_LIMIT_MGMT_DSPACE_INFO_LIST_COUNT * \
    sizeof(struct PVFS_mgmt_dspace_info))

/* mgmt_event_mon **************************************/
/* - returns event logging data */

struct PVFS_servreq_mgmt_event_mon
{
    uint32_t event_count;
};
endecode_fields_1_struct(
    PVFS_servreq_mgmt_event_mon,
    uint32_t, event_count)

#define PINT_SERVREQ_MGMT_EVENT_MON_FILL(__req, __creds, __event_count)\
do {                                                                   \
    memset(&(__req), 0, sizeof(__req));                                \
    (__req).op = PVFS_SERV_MGMT_EVENT_MON;                             \
    (__req).credentials = (__creds);                                   \
    (__req).u.mgmt_event_mon.event_count = (__event_count);            \
} while (0)

struct PVFS_servresp_mgmt_event_mon
{
    struct PVFS_mgmt_event* event_array;
    uint32_t event_count;
};
endecode_fields_1a_struct(
    PVFS_servresp_mgmt_event_mon,
    skip4,,
    uint32_t, event_count,
    PVFS_mgmt_event, event_array)
#define extra_size_PVFS_servresp_mgmt_event_mon \
  (PVFS_REQ_LIMIT_MGMT_EVENT_MON_COUNT * \
   roundup8(sizeof(struct PVFS_mgmt_event)))

/* geteattr ****************************************************/
/* - retrieves list of extended attributes */

struct PVFS_servreq_geteattr
{
    PVFS_handle handle;  /* handle of target object */
    PVFS_fs_id fs_id;    /* file system */
    int32_t nkey;        /* number of keys to read */
    PVFS_ds_keyval *key; /* array of keys to read */
    PVFS_size *valsz;    /* array of value buffer sizes */
};
endecode_fields_2aa_struct(
    PVFS_servreq_geteattr,
    PVFS_handle, handle,
    PVFS_fs_id, fs_id,
    int32_t, nkey,
    PVFS_ds_keyval, key,
    PVFS_size, valsz);
#define extra_size_PVFS_servreq_geteattr \
    (PVFS_REQ_LIMIT_KEY_LEN * PVFS_REQ_LIMIT_KEYVAL_LIST)

#define PINT_SERVREQ_GETEATTR_FILL(__req,   \
                                  __creds, \
                                  __fsid,  \
                                  __handle,\
                                  __nkey,\
                                  __key_array, \
                                  __size_array) \
do {                                       \
    memset(&(__req), 0, sizeof(__req));    \
    (__req).op = PVFS_SERV_GETEATTR;        \
    (__req).credentials = (__creds);       \
    (__req).u.geteattr.fs_id = (__fsid);    \
    (__req).u.geteattr.handle = (__handle); \
    (__req).u.geteattr.nkey = (__nkey); \
    (__req).u.geteattr.key = (__key_array);\
    (__req).u.geteattr.valsz = (__size_array);\
} while (0)

struct PVFS_servresp_geteattr
{
    int32_t nkey;           /* number of values returned */
    PVFS_ds_keyval *val;    /* array of values returned */
    PVFS_error *err;        /* array of error codes */
};
endecode_fields_1aa_struct(
    PVFS_servresp_geteattr,
    skip4,,
    int32_t, nkey,
    PVFS_ds_keyval, val,
    PVFS_error, err);
#define extra_size_PVFS_servresp_geteattr \
    (PVFS_REQ_LIMIT_VAL_LEN * PVFS_REQ_LIMIT_KEYVAL_LIST + \
    PVFS_REQ_LIMIT_KEYVAL_LIST * sizeof(PVFS_error))

/* seteattr ****************************************************/
/* - sets list of extended attributes */

struct PVFS_servreq_seteattr
{
    PVFS_handle handle;    /* handle of target object */
    PVFS_fs_id fs_id;      /* file system */
    int32_t    flags;      /* flags */
    int32_t    nkey;       /* number of keys and vals */
    PVFS_ds_keyval *key;    /* attribute key */
    PVFS_ds_keyval *val;    /* attribute value */
};
endecode_fields_4aa_struct(
    PVFS_servreq_seteattr,
    PVFS_handle, handle,
    PVFS_fs_id, fs_id,
    int32_t, flags,
    skip4,,
    int32_t, nkey,
    PVFS_ds_keyval, key,
    PVFS_ds_keyval, val);
#define extra_size_PVFS_servreq_seteattr \
    ((PVFS_REQ_LIMIT_KEY_LEN + PVFS_REQ_LIMIT_VAL_LEN) \
        * PVFS_REQ_LIMIT_KEYVAL_LIST)

#define PINT_SERVREQ_SETEATTR_FILL(__req,   \
                                  __creds,       \
                                  __fsid,        \
                                  __handle,      \
                                  __flags,       \
                                  __nkey,        \
                                  __key_array,   \
                                  __val_array)   \
do {                                             \
    memset(&(__req), 0, sizeof(__req));          \
    (__req).op = PVFS_SERV_SETEATTR;        \
    (__req).credentials = (__creds);             \
    (__req).u.seteattr.fs_id = (__fsid);    \
    (__req).u.seteattr.handle = (__handle); \
    (__req).u.seteattr.flags = (__flags);   \
    (__req).u.seteattr.nkey = (__nkey);     \
    (__req).u.seteattr.key = (__key_array); \
    (__req).u.seteattr.val = (__val_array); \
} while (0)

/* deleattr ****************************************************/
/* - deletes extended attributes */

struct PVFS_servreq_deleattr
{
    PVFS_handle handle; /* handle of target object */
    PVFS_fs_id fs_id;   /* file system */
    PVFS_ds_keyval key; /* key to read */
};
endecode_fields_3_struct(
    PVFS_servreq_deleattr,
    PVFS_handle, handle,
    PVFS_fs_id, fs_id,
    PVFS_ds_keyval, key);
#define extra_size_PVFS_servreq_deleattr \
    PVFS_REQ_LIMIT_KEY_LEN

#define PINT_SERVREQ_DELEATTR_FILL(__req,   \
                                  __creds, \
                                  __fsid,  \
                                  __handle,\
                                  __key) \
do {                                       \
    memset(&(__req), 0, sizeof(__req));    \
    (__req).op = PVFS_SERV_DELEATTR;        \
    (__req).credentials = (__creds);       \
    (__req).u.deleattr.fs_id = (__fsid);    \
    (__req).u.deleattr.handle = (__handle); \
    (__req).u.deleattr.key.buffer_sz = (__key).buffer_sz;\
    (__req).u.deleattr.key.buffer = (__key).buffer;\
} while (0)

/* listeattr **************************************************/
/* - list extended attributes */

struct PVFS_servreq_listeattr
{
    PVFS_handle handle;     /* handle of dir object */
    PVFS_fs_id  fs_id;      /* file system */
    PVFS_ds_position token; /* offset */
    uint32_t     nkey;      /* desired number of keys to read */
    PVFS_size   *keysz;     /* array of key buffer sizes */
};
endecode_fields_4a_struct(
    PVFS_servreq_listeattr,
    PVFS_handle, handle,
    PVFS_fs_id, fs_id,
    PVFS_ds_position, token,
    skip4,,
    uint32_t, nkey,
    PVFS_size, keysz);
#define extra_size_PVFS_servreq_listeattr \
    (PVFS_REQ_LIMIT_KEYVAL_LIST * sizeof(PVFS_size))

#define PINT_SERVREQ_LISTEATTR_FILL(__req,            \
                                  __creds,            \
                                  __fsid,             \
                                  __handle,           \
                                  __token,            \
                                  __nkey,             \
                                  __size_array)       \
do {                                                  \
    memset(&(__req), 0, sizeof(__req));               \
    (__req).op = PVFS_SERV_LISTEATTR;                 \
    (__req).credentials = (__creds);                  \
    (__req).u.listeattr.fs_id = (__fsid);             \
    (__req).u.listeattr.handle = (__handle);          \
    (__req).u.listeattr.token = (__token);            \
    (__req).u.listeattr.nkey = (__nkey);              \
    (__req).u.listeattr.keysz = (__size_array);       \
} while (0);

struct PVFS_servresp_listeattr
{
    PVFS_ds_position token;  /* new dir offset */
    uint32_t nkey;   /* # of keys retrieved */
    PVFS_ds_keyval *key; /* array of keys returned */
};
endecode_fields_1a_struct(
    PVFS_servresp_listeattr,
    PVFS_ds_position, token,
    uint32_t, nkey,
    PVFS_ds_keyval, key)
#define extra_size_PVFS_servresp_listeattr \
    (PVFS_REQ_LIMIT_KEY_LEN * PVFS_REQ_LIMIT_KEYVAL_LIST)


/* server request *********************************************/
/* - generic request with union of all op specific structs */

struct PVFS_server_req
{
    enum PVFS_server_op op;
    PVFS_credentials credentials;
    union
    {
        struct PVFS_servreq_create create;
        struct PVFS_servreq_remove remove;
	struct PVFS_servreq_lock lock;
        struct PVFS_servreq_io io;
        struct PVFS_servreq_getattr getattr;
        struct PVFS_servreq_setattr setattr;
        struct PVFS_servreq_mkdir mkdir;
        struct PVFS_servreq_readdir readdir;
        struct PVFS_servreq_lookup_path lookup_path;
        struct PVFS_servreq_crdirent crdirent;
        struct PVFS_servreq_rmdirent rmdirent;
        struct PVFS_servreq_chdirent chdirent;
        struct PVFS_servreq_truncate truncate;
        struct PVFS_servreq_flush flush;
        struct PVFS_servreq_mgmt_setparam mgmt_setparam;
        struct PVFS_servreq_statfs statfs;
        struct PVFS_servreq_mgmt_perf_mon mgmt_perf_mon;
        struct PVFS_servreq_mgmt_iterate_handles mgmt_iterate_handles;
        struct PVFS_servreq_mgmt_dspace_info_list mgmt_dspace_info_list;
        struct PVFS_servreq_mgmt_event_mon mgmt_event_mon;
        struct PVFS_servreq_mgmt_remove_object mgmt_remove_object;
        struct PVFS_servreq_mgmt_remove_dirent mgmt_remove_dirent;
        struct PVFS_servreq_mgmt_get_dirdata_handle mgmt_get_dirdata_handle;
        struct PVFS_servreq_geteattr geteattr;
        struct PVFS_servreq_seteattr seteattr;
        struct PVFS_servreq_deleattr deleattr;
        struct PVFS_servreq_listeattr listeattr;
        struct PVFS_servreq_small_io small_io;
    } u;
};
#ifdef __PINT_REQPROTO_ENCODE_FUNCS_C
/* insert padding to ensure the union starts on an aligned boundary */
static inline void
encode_PVFS_server_req(char **pptr, const struct PVFS_server_req *x) {
    encode_enum(pptr, &x->op);
    *pptr += 4;
    encode_PVFS_credentials(pptr, &x->credentials);
}
static inline void
decode_PVFS_server_req(char **pptr, struct PVFS_server_req *x) {
    decode_enum(pptr, &x->op);
    *pptr += 4;
    decode_PVFS_credentials(pptr, &x->credentials);
}
#endif

/* server response *********************************************/
/* - generic response with union of all op specific structs */
struct PVFS_server_resp
{
    enum PVFS_server_op op;
    PVFS_error status;
    union
    {
        struct PVFS_servresp_create create;
        struct PVFS_servresp_getattr getattr;
        struct PVFS_servresp_mkdir mkdir;
        struct PVFS_servresp_readdir readdir;
        struct PVFS_servresp_lookup_path lookup_path;
        struct PVFS_servresp_rmdirent rmdirent;
        struct PVFS_servresp_chdirent chdirent;
        struct PVFS_servresp_getconfig getconfig;
	struct PVFS_servresp_lock lock;
        struct PVFS_servresp_io io;
        struct PVFS_servresp_write_completion write_completion;
        struct PVFS_servresp_statfs statfs;
        struct PVFS_servresp_mgmt_setparam mgmt_setparam;
        struct PVFS_servresp_mgmt_perf_mon mgmt_perf_mon;
        struct PVFS_servresp_mgmt_iterate_handles mgmt_iterate_handles;
        struct PVFS_servresp_mgmt_dspace_info_list mgmt_dspace_info_list;
        struct PVFS_servresp_mgmt_event_mon mgmt_event_mon;
        struct PVFS_servresp_mgmt_get_dirdata_handle mgmt_get_dirdata_handle;
        struct PVFS_servresp_geteattr geteattr;
        struct PVFS_servresp_listeattr listeattr;
        struct PVFS_servresp_small_io small_io;
    } u;
};
endecode_fields_2_struct(
    PVFS_server_resp,
    enum, op,
    PVFS_error, status)

#endif /* __PVFS2_REQ_PROTO_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
