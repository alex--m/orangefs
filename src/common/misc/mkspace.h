/*
 * (C) 2002 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __MKSPACE_H
#define __MKSPACE_H

#include "trove.h"

int pvfs2_mkspace(
    char *storage_space,
    char *collection,
    TROVE_coll_id coll_id,
    int root_handle,
    char *handle_ranges,
    int create_collection_only,
    int verbose);

#endif /* __MKSPACE_H */
