/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <bmi.h>
#include <pvfs2-req-proto.h>
#include <gossip.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <PINT-reqproto-encode.h>
#include <PINT-reqproto-module.h>
#include <assert.h>

DECODE_RESP_HEAD(do_decode_resp)
{

	struct PVFS_server_resp_s *response = input_buffer;
	struct PVFS_server_resp_s *decoded_response = NULL;

	assert(response->rsize != 0);

	target_msg->buffer = malloc(response->rsize);
	memcpy(target_msg->buffer,response,response->rsize);

	decoded_response = (struct PVFS_server_resp_s*)target_msg->buffer;

	switch(response->op)
	{

		case PVFS_SERV_GETCONFIG:
                    ((struct PVFS_server_resp_s *)target_msg->buffer)->u.getconfig.fs_config_buf
                        = (char *)(target_msg->buffer + sizeof(struct PVFS_server_resp_s));
                    ((struct PVFS_server_resp_s *)target_msg->buffer)->u.getconfig.server_config_buf
                        = (char *)(target_msg->buffer + sizeof(struct PVFS_server_resp_s) +
                            response->u.getconfig.fs_config_buflen);
                    return 0;
		case PVFS_SERV_LOOKUP_PATH:
			((struct PVFS_server_resp_s *)target_msg->buffer)->u.lookup_path.handle_array = 
							(PVFS_handle *) ((char*)target_msg->buffer + 
							  sizeof(struct PVFS_server_resp_s));
			((struct PVFS_server_resp_s *)target_msg->buffer)->u.lookup_path.attr_array = 
						(PVFS_object_attr *) ((char*)target_msg->buffer 
						+ sizeof(struct PVFS_server_resp_s) + 
						sizeof(PVFS_handle)*response->u.lookup_path.count);
			return 0;

		case PVFS_SERV_READDIR:
			((struct PVFS_server_resp_s *)target_msg->buffer)->u.readdir.pvfs_dirent_array =
					(PVFS_dirent *)(target_msg->buffer+sizeof(struct PVFS_server_resp_s));
			return 0;

		case PVFS_SERV_GETATTR:
			if(decoded_response->u.getattr.attr.objtype ==
				PVFS_TYPE_METAFILE)
			{
				decoded_response->u.getattr.attr.u.meta.dfh = 
					(PVFS_handle*)(((char*)decoded_response) 
					+ sizeof(struct PVFS_server_resp_s));
				decoded_response->u.getattr.attr.u.meta.dist = 
					(PVFS_Dist*)(((char*)decoded_response)
					+ sizeof(struct PVFS_server_resp_s)
					+ (decoded_response->u.getattr.attr.u.meta.nr_datafiles 
					* sizeof(PVFS_handle)));
				PINT_Dist_decode(decoded_response->u.getattr.attr.u.meta.dist,
					NULL);
			}
			return 0;
		case PVFS_SERV_CREATE:
		case PVFS_SERV_NOOP:
		case PVFS_SERV_SETATTR:
		case PVFS_SERV_REMOVE:
		case PVFS_SERV_CREATEDIRENT:
		case PVFS_SERV_MKDIR:
		case PVFS_SERV_RMDIRENT:
		case PVFS_SERV_IO:
		case PVFS_SERV_WRITE_COMPLETION:
			return 0;
		default:
			return -1;
	}
	return -1;
}
