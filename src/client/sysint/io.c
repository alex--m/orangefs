/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* I/O Function Implementation */
#include <malloc.h>
#include <assert.h>
#include <string.h>

#include <pinode-helper.h>
#include <pvfs2-sysint.h>
#include <pint-sysint.h>
#include <pvfs2-req-proto.h>
#include <pvfs-distribution.h>
#include <pint-servreq.h>
#include <pint-bucket.h>
#include <PINT-reqproto-encode.h>

#define REQ_ENC_FORMAT 0

/* PVFS_sys_io()
 *
 * performs a read or write operation.  PVFS_sys_read() and
 * PVFS_sys_write() are aliases to this function.
 *
 * returns 0 on success, -errno on failure
 */
int PVFS_sys_io(PVFS_sysreq_io *req, PVFS_sysresp_io *resp,
    enum PVFS_sys_io_type type)
{
    pinode* pinode_ptr = NULL;
    PVFS_bitfield attr_mask = 0;
    int ret = -1;

    /* find a pinode for the target file */

    /* TODO: what should the attr_mask be set to? */
    attr_mask = ATTR_BASIC;
    ret = phelper_get_pinode(req->pinode_refn, &pinode_ptr,
	attr_mask, req->credentials);
    if(ret < 0)
    {
	/* TODO: handle this error */
	gossip_lerr("foo");
	return(-1);
    }

    /* at this point, we need to have a list of handles and
     * distribution information 
     * - the dist we can make up for now
     * - however, we need to get the datafile handle from somewhere...
     *   - for now, I could just create one directly in the test program
     *     pass its handle in?
     */

    return(-ENOSYS);
#if 0
	struct PVFS_server_req_s req_p;	 	/* server request */
	struct PVFS_server_resp_s *ack_p = NULL; /* server response */
   	int ret = -1;
   	bmi_addr_t serv_addr;	            /* PVFS address type structure */ 
	char *server = NULL;
	struct timeval cur_time;
	PVFS_size *size_array = 0;
	pinode *entry_pinode = NULL;
	PVFS_bitfield attr_mask = req->attrmask;
	pinode_reference entry;
	struct PINT_decoded_msg decoded;
	int max_msg_sz = 0;
	int pinode_exists_in_cache = 0;

	enum {
	    NONE_FAILURE = 0,
	    MAP_SERVER_FAILURE,
	    SEND_REQ_FAILURE,
	    MALLOC_DFH_FAILURE,
	    PCACHE_INSERT_FAILURE,
	} failure = NONE_FAILURE;

	/* Let's check if size is to be fetched here, If so
	 * somehow ensure that dist is returned as part of 
	 * attributes - is this the way we want this to be done?
	 */

	if (req->attrmask & ATTR_SIZE)
		attr_mask |= ATTR_META;

	/* Fill in pinode reference */ 
	entry.handle = req->pinode_refn.handle;
	entry.fs_id = req->pinode_refn.fs_id;

	/* do we have a valid copy? 
	 * if any of the attributes are stale, or absent then we need to 
	 * retrive a fresh copy.
	 */
	ret = PINT_pcache_lookup(entry, &entry_pinode);
	if (ret  == PCACHE_LOOKUP_SUCCESS)
        {
		resp->attr = entry_pinode->attr;
		if ((req->attrmask & ATTR_SIZE) == ATTR_SIZE)
		{
			/* if we want the size, and its valid, then return now */
			if (entry_pinode->size_flag == SIZE_VALID)
			{
			    resp->attr = entry_pinode->attr;
			    /* resp->extended */
			    return (0);
			}

			/* if the pinode already exists in the cache, we need
			 * to remember this so we can update fields instead
			 * of adding it again.
			 */
			pinode_exists_in_cache = 1;
			/* if the size isn't valid, continue with the getattr*/
		}
		else
		{
		    /* if we don't care about size in our request, we're done already */
		    resp->attr = entry_pinode->attr;
		    /* resp->extended */
		    return (0);
		}
        }
	else
	{
		/* setup new pinode that we'll add to the cache */
		ret = PINT_pcache_pinode_alloc( &entry_pinode );
		if (ret < 0)
		{
			failure = NONE_FAILURE; /* nothing to dealloc, but still need to fail in error */
			goto return_error;
		}
		entry_pinode->pinode_ref.handle = entry.handle;
		entry_pinode->pinode_ref.fs_id = entry.fs_id;
		entry_pinode->size_flag = SIZE_INVALID;
	}

	ret = PINT_bucket_map_to_server(&serv_addr,entry.handle,entry.fs_id);
        if (ret < 0)
        {
		failure = MAP_SERVER_FAILURE;
		goto return_error;
        }

	req_p.op = PVFS_SERV_GETATTR;
        req_p.credentials = req->credentials;
	req_p.rsize = sizeof(struct PVFS_server_req_s);
	req_p.u.getattr.handle = entry.handle;
	req_p.u.getattr.fs_id = entry.fs_id;
	req_p.u.getattr.attrmask = req->attrmask;

	/* Q: how much stuff are we getting back? */
	max_msg_sz = sizeof(struct PVFS_server_resp_s);

	/* Make a server getattr request */
	ret = PINT_server_send_req(serv_addr, &req_p, max_msg_sz, &decoded);
	if (ret < 0)
	{
		failure = SEND_REQ_FAILURE;
		goto return_error;
	}

	ack_p = (struct PVFS_server_resp_s *) decoded.buffer;

	if (ack_p->status < 0 )
        {
		ret = ack_p->status;
		failure = SEND_REQ_FAILURE;
		goto return_error;
        }

	resp->attr = ack_p->u.getattr.attr;
    /*the server isn't returning the data handles yet, put this back in when
     * dale fixes it */
#if 0
	if (resp->attr.objtype == ATTR_META)
	{
	    if(resp->attr.u.meta.nr_datafiles > 0)
	    {
		assert(ack_p->u.getattr.attr.u.meta.dfh != NULL);

		resp->attr.u.meta.dfh = malloc(resp->attr.u.meta.nr_datafiles * sizeof(PVFS_handle));
		if (resp->attr.u.meta.dfh ==  NULL)
		{
		    ret = (-ENOMEM);
		    failure = MALLOC_DFH_FAILURE;
		    goto return_error;
		}
		memcpy(	resp->attr.u.meta.dfh, 
			ack_p->u.getattr.attr.u.meta.dfh, 
			resp->attr.u.meta.nr_datafiles * sizeof(PVFS_handle));
	    }
	}
#endif
	
	/* TODO: copy extended attributes just like normal attr */
	/* resp->eattr = ack_p.u.getattr.eattr; */

	PINT_decode_release(&decoded, PINT_DECODE_RESP, REQ_ENC_FORMAT);

	/* do size calculations here? */

	if ((req->attrmask & ATTR_SIZE) == ATTR_SIZE)
	{
	    /* getting size isn't implemented yet, figure this out,
	     * and uncomment.
	     */
	    return (-EINVAL);
#if 0
		/*only do this if you want the size*/

		/* TODO: things to do to get the size:
		 * 1). get each handle
		 * 2). for each handle find out how much data is written on each (dist code)
		 * 3). uhm, I think that's it?
		 */

		num_data_servers = entry_pinode->attr.u.meta.nr_datafiles;
		/**/

		entry_pinode->size_flag = SIZE_VALID;
#endif
	}
	else
	{
	    /* if we get everything but the size, the updated pinode timestamp
	     * doesn't have anything to do with the size.
	     */
	    entry_pinode->size_flag = SIZE_INVALID;
	}

	ret = gettimeofday(&cur_time,NULL);
	if (ret < 0)
	{
		failure = PCACHE_INSERT_FAILURE;
		goto return_error;
	}
	/* Set the size timestamp */
	phelper_fill_timestamps(entry_pinode);

	if (pinode_exists_in_cache == 0)
	{
	    /* Add to cache  */
	    ret = PINT_pcache_insert(entry_pinode);
	    if (ret < 0)
	    {
		failure = PCACHE_INSERT_FAILURE;
		goto return_error;
	    }
	    printf("GETATTR:  ADDING TO PCACHE\n");
	}
	else
	{
	    printf("GETATTR:   NOT ADDING TO PCACHE\n");
	}

	/* Free memory allocated for name */
	if (size_array)
	    free(size_array);


	return(0);

return_error:

	switch( failure ) 
	{
		case PCACHE_INSERT_FAILURE:
		    free(resp->attr.u.meta.dfh);
		case MALLOC_DFH_FAILURE:
		case SEND_REQ_FAILURE:
		case MAP_SERVER_FAILURE:
			if (ack_p)
				PINT_decode_release(&decoded, PINT_DECODE_RESP, REQ_ENC_FORMAT);

			if (server)
				free(server);
			/* Free memory allocated for name */
			if (size_array)
				free(size_array);
		case NONE_FAILURE:
		/**/
	}

	return(ret);
#endif
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
