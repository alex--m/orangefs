/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __PINT_CACHED_CONFIG_H
#define __PINT_CACHED_CONFIG_H

#include "pvfs2-types.h"
#include "pvfs2-storage.h"
#include "pvfs2-mgmt.h"
#include "bmi.h"
#include "trove.h"
#include "server-config.h"

#define PINT_SERVER_TYPE_IO                            PVFS_MGMT_IO_SERVER
#define PINT_SERVER_TYPE_META                        PVFS_MGMT_META_SERVER
#define PINT_SERVER_TYPE_ALL   (PINT_SERVER_TYPE_META|PINT_SERVER_TYPE_IO)

/* This is the interface to the cached_config management component of
 * the system interface.  It is responsible for caching information
 * gathered from the various server configurations and providing an
 * interface that allows fast access to that information.
 */

int PINT_cached_config_initialize(void);

int PINT_cached_config_finalize(void);

int PINT_handle_load_mapping(
    struct server_configuration_s *config,
    struct filesystem_configuration_s *fs);

int PINT_cached_config_get_next_meta(
    struct server_configuration_s *config,
    PVFS_fs_id fsid,
    PVFS_BMI_addr_t *meta_addr,
    PVFS_handle_extent_array *meta_extent_array);

int PINT_cached_config_get_next_io(
    struct server_configuration_s *config,
    PVFS_fs_id fsid,
    int num_servers,
    PVFS_BMI_addr_t *io_addr_array,
    PVFS_handle_extent_array *io_handle_extent_array);

const char *PINT_cached_config_map_addr(
    struct server_configuration_s *config,
    PVFS_fs_id fsid,
    PVFS_BMI_addr_t addr,
    int *server_type);
 
int PINT_cached_config_get_server_array(
    struct server_configuration_s *config,
    PVFS_fs_id fsid,
    int server_type,
    PVFS_BMI_addr_t *addr_array,
    int *inout_count_p);

int PINT_cached_config_count_servers(
    struct server_configuration_s *config,
    PVFS_fs_id fsid,
    int server_type,
    int *count);

int PINT_cached_config_map_to_server(
    PVFS_BMI_addr_t *server_addr,
    PVFS_handle handle,
    PVFS_fs_id fsid);

int PINT_cached_config_get_num_dfiles(
    PVFS_fs_id fsid,
    PINT_dist *dist,
    int num_dfiles_requested,
    int *num_dfiles);

int PINT_cached_config_get_num_meta(
    PVFS_fs_id fsid,
    int *num_meta);

int PINT_cached_config_get_num_io(
    PVFS_fs_id fsid,
    int *num_io);

int PINT_cached_config_get_server_name(
    char *server_name,
    int max_server_name_len,
    PVFS_handle handle,
    PVFS_fs_id fsid);

int PINT_cached_config_get_server_handle_count(
    const char *server_addr_str,
    PVFS_fs_id fs_id,
    uint64_t *handle_count);
    
int PINT_cached_config_get_root_handle(
    PVFS_fs_id fsid,
    PVFS_handle *fh_root);

int PINT_cached_config_get_handle_timeout(
    PVFS_fs_id fsid,
    struct timeval *timeout);

int PINT_cached_config_reinitialize(
    struct server_configuration_s *config);

#endif /* __PINT_CACHED_CONFIG_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
