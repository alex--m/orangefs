/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * Changes by Acxiom Corporation to add PINT_check_mode() helper function
 * as a replacement for check_mode() in permission checking, also added
 * PINT_check_group() for supplimental group support 
 * Copyright � Acxiom Corporation, 2005.
 *
 * See COPYING in top-level directory.
 */

/* This file includes definitions of common internal utility functions */
#include <string.h>
#include <assert.h>
#include <sys/time.h>
#include <sys/resource.h>

#include "gen-locks.h"
#include "pint-util.h"

void PINT_time_mark(PINT_time_marker *out_marker)
{
    struct rusage usage;

    gettimeofday(&out_marker->wtime, NULL);
    getrusage(RUSAGE_SELF, &usage);
    out_marker->utime = usage.ru_utime;
    out_marker->stime = usage.ru_stime;
}

void PINT_time_diff(PINT_time_marker mark1, 
                    PINT_time_marker mark2,
                    double *out_wtime_sec,
                    double *out_utime_sec,
                    double *out_stime_sec)
{
    *out_wtime_sec = 
        ((double)mark2.wtime.tv_sec +
         (double)(mark2.wtime.tv_usec) / 1000000) -
        ((double)mark1.wtime.tv_sec +
         (double)(mark1.wtime.tv_usec) / 1000000);

    *out_stime_sec = 
        ((double)mark2.stime.tv_sec +
         (double)(mark2.stime.tv_usec) / 1000000) -
        ((double)mark1.stime.tv_sec +
         (double)(mark1.stime.tv_usec) / 1000000);

    *out_utime_sec = 
        ((double)mark2.utime.tv_sec +
         (double)(mark2.utime.tv_usec) / 1000000) -
        ((double)mark1.utime.tv_sec +
         (double)(mark1.utime.tv_usec) / 1000000);
}

static int current_tag = 1;
static gen_mutex_t current_tag_lock = GEN_MUTEX_INITIALIZER;

PVFS_msg_tag_t PINT_util_get_next_tag(void)
{
    PVFS_msg_tag_t ret;

    gen_mutex_lock(&current_tag_lock);
    ret = current_tag;

    /* increment the tag, don't use zero */
    if (current_tag + 1 == PINT_MSG_TAG_INVALID)
    {
	current_tag = 1;
    }
    else
    {
	current_tag++;
    }
    gen_mutex_unlock(&current_tag_lock);

    return ret;
}

int PINT_copy_object_attr(PVFS_object_attr *dest, PVFS_object_attr *src)
{
    int ret = -PVFS_ENOMEM;

    if (dest && src)
    {
	if (src->mask & PVFS_ATTR_COMMON_UID)
        {
            dest->owner = src->owner;
        }
	if (src->mask & PVFS_ATTR_COMMON_GID)
        {
            dest->group = src->group;
        }
	if (src->mask & PVFS_ATTR_COMMON_PERM)
        {
            dest->perms = src->perms;
        }
	if (src->mask & PVFS_ATTR_COMMON_ATIME)
        {
            dest->atime = src->atime;
        }
	if (src->mask & PVFS_ATTR_COMMON_CTIME)
        {
            dest->ctime = src->ctime;
        }
        if (src->mask & PVFS_ATTR_COMMON_MTIME)
        {
            dest->mtime = src->mtime;
        }
	if (src->mask & PVFS_ATTR_COMMON_TYPE)
        {
            dest->objtype = src->objtype;
        }

        if (src->mask & PVFS_ATTR_DIR_DIRENT_COUNT)
        {
            dest->u.dir.dirent_count = 
                src->u.dir.dirent_count;
        }

        if (src->mask & PVFS_ATTR_DIR_HINT)
        {
            dest->u.dir.hint.dfile_count = 
                src->u.dir.hint.dfile_count;
            dest->u.dir.hint.dist_name_len =
                src->u.dir.hint.dist_name_len;
            if (dest->u.dir.hint.dist_name_len > 0)
            {
                dest->u.dir.hint.dist_name = strdup(src->u.dir.hint.dist_name);
                if (dest->u.dir.hint.dist_name == NULL)
                {
                    return ret;
                }
            }
            dest->u.dir.hint.dist_params_len =
                src->u.dir.hint.dist_params_len;
            if (dest->u.dir.hint.dist_params_len > 0)
            {
                dest->u.dir.hint.dist_params = strdup(src->u.dir.hint.dist_params);
                if (dest->u.dir.hint.dist_params == NULL)
                {
                    free(dest->u.dir.hint.dist_name);
                    return ret;
                }
            }
        }

        /*
          NOTE:
          we only copy the size out if we're actually a
          datafile object.  sometimes the size field is
          valid when the objtype is a metafile because
          of different uses of the acache.  In this case
          (namely, getattr), the size is stored in the
          acache before this deep copy, so it's okay
          that we're not copying here even though the
          size mask bit is set.

          if we don't do this trick, the metafile that
          caches the size will have it's union data
          overwritten with a bunk size.
        */
        if ((src->mask & PVFS_ATTR_DATA_SIZE) &&
            (src->mask & PVFS_ATTR_COMMON_TYPE) &&
            (src->objtype == PVFS_TYPE_DATAFILE))
        {
            dest->u.data.size = src->u.data.size;
        }

	if ((src->mask & PVFS_ATTR_COMMON_TYPE) &&
            (src->objtype == PVFS_TYPE_METAFILE))
        {      
            if(src->mask & PVFS_ATTR_META_DFILES)
            {
                PVFS_size df_array_size = src->u.meta.dfile_count *
                    sizeof(PVFS_handle);

                if (df_array_size)
                {
                    if ((dest->mask & PVFS_ATTR_META_DFILES) &&
                        dest->u.meta.dfile_count > 0)
                    {
                        if (dest->u.meta.dfile_array)
                        {
                            free(dest->u.meta.dfile_array);
                            dest->u.meta.dfile_array = NULL;
                        }
                    }
                    dest->u.meta.dfile_array = malloc(df_array_size);
                    if (!dest->u.meta.dfile_array)
                    {
                        return ret;
                    }
                    memcpy(dest->u.meta.dfile_array,
                           src->u.meta.dfile_array, df_array_size);
                }
                else
                {
                    dest->u.meta.dfile_array = NULL;
                }
                dest->u.meta.dfile_count = src->u.meta.dfile_count;
            }

            if(src->mask & PVFS_ATTR_META_DIST)
            {
                assert(src->u.meta.dist_size > 0);

                if ((dest->mask & PVFS_ATTR_META_DIST) && dest->u.meta.dist)
                {
                    PINT_dist_free(dest->u.meta.dist);
                }
                dest->u.meta.dist = PINT_dist_copy(src->u.meta.dist);
                if (dest->u.meta.dist == NULL)
                {
                    return ret;
                }
                dest->u.meta.dist_size = src->u.meta.dist_size;
            }
            memcpy(&dest->u.meta.hint, &src->u.meta.hint, sizeof(dest->u.meta.hint));
        }

        if (src->mask & PVFS_ATTR_SYMLNK_TARGET)
        {
            dest->u.sym.target_path_len = src->u.sym.target_path_len;
            dest->u.sym.target_path = strdup(src->u.sym.target_path);
            if (dest->u.sym.target_path == NULL)
            {
                return ret;
            }
        }

	dest->mask = src->mask;
        ret = 0;
    }
    return ret;
}

void PINT_free_object_attr(PVFS_object_attr *attr)
{
    if (attr)
    {
        if (attr->objtype == PVFS_TYPE_METAFILE)
        {
            if (attr->mask & PVFS_ATTR_META_DFILES)
            {
                if (attr->u.meta.dfile_array)
                {
                    free(attr->u.meta.dfile_array);
                    attr->u.meta.dfile_array = NULL;
                }
            }
            if (attr->mask & PVFS_ATTR_META_DIST)
            {
                if (attr->u.meta.dist)
                {
                    PINT_dist_free(attr->u.meta.dist);
                    attr->u.meta.dist = NULL;
                }
            }
        }
        else if (attr->objtype == PVFS_TYPE_SYMLINK)
        {
            if (attr->mask & PVFS_ATTR_SYMLNK_TARGET)
            {
                if ((attr->u.sym.target_path_len > 0) &&
                    attr->u.sym.target_path)
                {
                    free(attr->u.sym.target_path);
                }
            }
        }
        else if (attr->objtype == PVFS_TYPE_DIRECTORY)
        {
            if ((attr->mask & PVFS_ATTR_DIR_HINT) || (attr->mask & PVFS_ATTR_DIR_DIRENT_COUNT))
            {
                if (attr->u.dir.hint.dist_name)
                {
                    free(attr->u.dir.hint.dist_name);
                }
                if (attr->u.dir.hint.dist_params)
                {
                    free(attr->u.dir.hint.dist_params);
                }
            }
        }
    }
}

char *PINT_util_get_object_type(int objtype)
{
    static char *obj_types[] =
    {
         "NONE", "METAFILE", "DATAFILE",
         "DIRECTORY", "SYMLINK", "DIRDATA", "UNKNOWN"
    };
    switch(objtype)
    {
    case PVFS_TYPE_NONE:
         return obj_types[0];
    case PVFS_TYPE_METAFILE:
         return obj_types[1];
    case PVFS_TYPE_DATAFILE:
         return obj_types[2];
    case PVFS_TYPE_DIRECTORY:
         return obj_types[3];
    case PVFS_TYPE_SYMLINK:
         return obj_types[4];
    case PVFS_TYPE_DIRDATA:
         return obj_types[5];
    }
    return obj_types[6];
}

PVFS_time PINT_util_get_current_time(void)
{
    struct timeval t = {0,0};
    PVFS_time current_time = 0;

    gettimeofday(&t, NULL);
    current_time = (PVFS_time)t.tv_sec;
    return current_time;
}

PVFS_time PINT_util_mktime_version(PVFS_time time)
{
    struct timeval t = {0,0};
    PVFS_time version = (time << 32);

    gettimeofday(&t, NULL);
    version |= (PVFS_time)t.tv_usec;
    return version;
}

PVFS_time PINT_util_mkversion_time(PVFS_time version)
{
    return (PVFS_time)(version >> 32);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
