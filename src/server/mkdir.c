/* 
 * (C) 2001 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */


#include <state-machine.h>
#include <server-config.h>
#include <pvfs2-server.h>
#include <string.h>
#include <pvfs2-attr.h>
#include <assert.h>

static int mkdir_init(PINT_server_op *s_op, job_status_s *ret);
static int mkdir_create(PINT_server_op *s_op, job_status_s *ret);
static int mkdir_setattrib(PINT_server_op *s_op, job_status_s *ret);
static int mkdir_release(PINT_server_op *s_op, job_status_s *ret);
static int mkdir_send_bmi(PINT_server_op *s_op, job_status_s *ret);
static int mkdir_cleanup(PINT_server_op *s_op, job_status_s *ret);
static int mkdir_error(PINT_server_op *s_op, job_status_s *ret);
static int mkdir_critical_error(PINT_server_op *s_op, job_status_s *ret);
void mkdir_init_state_machine(void);

extern PINT_server_trove_keys_s Trove_Common_Keys[];

PINT_state_machine_s mkdir_req_s = 
{
	NULL,
	"mkdir",
	mkdir_init_state_machine
};

%%

machine mkdir(init, create, set_attrib, release, send, err_msg, critical_error, cleanup)
{
	state init
	{
		run mkdir_init;
		success => create;
		default => err_msg;
	}

	state create
	{
		run mkdir_create;
		success => set_attrib;
		default => err_msg;
	}

	state set_attrib
	{
		run mkdir_setattrib;
		success => release;
		default => err_msg;
	}
	
	state release
	{
		run mkdir_release;
		success => send;
		default => critical_error;
	}

	state send
	{
		run mkdir_send_bmi;
		default => cleanup;
	}

	state err_msg 
	{
	    run mkdir_error;
	    default => release;
	}

	state critical_error 
	{
	    run mkdir_critical_error;
	    default => cleanup;
	}

	state cleanup
	{
		run mkdir_cleanup;
		default => init;
	}
}

%%

/*
 * Function: mkdir_init_state_machine
 *
 * Params:   void
 *
 * Returns:  PINT_state_array_values*
 *
 * Synopsis: Set up the state machine for mkdir. 
 *           
 */

void mkdir_init_state_machine(void)
{

    mkdir_req_s.state_machine = mkdir;

}

/*
 * Function: mkdir_init
 *
 * Params:   server_op *s_op, 
 *           job_status_s *ret
 *
 * Returns:  int
 *
 * Synopsis: 
 *           
 */


static int mkdir_init(PINT_server_op *s_op, job_status_s *ret)
{

    int job_post_ret;

    gossip_debug(SERVER_DEBUG, "mkdir state: init\n");

    job_post_ret = job_req_sched_post(s_op->req,
	    s_op,
	    ret,
	    &(s_op->scheduled_id));

    return(job_post_ret);

}

/*
 * Function: mkdir_create
 *
 * Params:   server_op *s_op, 
 *           job_status_s *ret
 *
 * Returns:  int
 *
 * Synopsis: 
 *
 * NOTE: returned handle will pop out in ret->handle (the job status struct).
 */
static int mkdir_create(PINT_server_op *s_op, job_status_s *ret)
{

    int job_post_ret;
    job_id_t i;

    gossip_debug(SERVER_DEBUG, "mkdir state: create\n");

    job_post_ret = job_trove_dspace_create(s_op->req->u.mkdir.fs_id,
	    s_op->req->u.mkdir.bucket,
	    s_op->req->u.mkdir.handle_mask,
	    ATTR_DIR,
	    NULL,
	    s_op,
	    ret,
	    &i);

    return(job_post_ret);
}


/*
 * Function: mkdir_setattrib
 *
 * Params:   server_op *s_op, 
 *           job_status_s *ret
 *
 * Returns:  int
 *
 * Synopsis: 
 *           
 */
static int mkdir_setattrib(PINT_server_op *s_op, job_status_s *ret)
{
    PVFS_object_attr *a_p;
    int job_post_ret;
    job_id_t i;

    gossip_debug(SERVER_DEBUG, "mkdir state: setattrib\n");

    /* save the handle from the create in the response */
    s_op->resp->u.mkdir.handle = ret->handle;

    s_op->key.buffer    = Trove_Common_Keys[METADATA_KEY].key;
    s_op->key.buffer_sz = Trove_Common_Keys[METADATA_KEY].size;

    s_op->val.buffer    = &(s_op->req->u.mkdir.attr);
    s_op->val.buffer_sz = sizeof(PVFS_object_attr);

    a_p = &(s_op->req->u.mkdir.attr);
    gossip_debug(SERVER_DEBUG,
		 "  attrs to write = (owner = %d, group = %d, perms = %o, type = %d)\n",
		 a_p->owner,
		 a_p->group,
		 a_p->perms,
		 a_p->objtype);

    gossip_debug(SERVER_DEBUG,
		 "  writing attributes (coll_id = 0x%x, handle = 0x%08Lx, key = %s (%d), val_buf = 0x%08x (%d))\n",
		 s_op->req->u.mkdir.fs_id,
		 s_op->resp->u.mkdir.handle,
		 (char *) s_op->key.buffer,
		 s_op->key.buffer_sz,
		 (unsigned) s_op->val.buffer,
		 s_op->val.buffer_sz);

    job_post_ret = job_trove_keyval_write(s_op->req->u.mkdir.fs_id,
					  s_op->resp->u.mkdir.handle,
					  &(s_op->key),
					  &(s_op->val),
					  TROVE_SYNC,
					  NULL,
					  s_op,
					  ret,
					  &i);

    return(job_post_ret);
}


/*
 * Function: mkdir_release
 *
 * Params:   server_op *b, 
 *           job_status_s *ret
 *
 * Pre:      We are done!
 *
 * Post:     We need to let the next operation go.
 *
 * Returns:  int
 *
 * Synopsis: Free the job from the scheduler to allow next job to proceed.
 */

static int mkdir_release(PINT_server_op *s_op, job_status_s *ret)
{

    int job_post_ret=0;
    job_id_t i;

    gossip_debug(SERVER_DEBUG, "mkdir state: release\n");

    job_post_ret = job_req_sched_release(s_op->scheduled_id,
	    s_op,
	    ret,
	    &i);
    return job_post_ret;
}


/*
 * Function: mkdir_bmi_send
 *
 * Params:   server_op *s_op, 
 *           job_status_s *ret
 *
 * Returns:  int
 *
 * Synopsis: 
 *           
 */


static int mkdir_send_bmi(PINT_server_op *s_op, job_status_s *ret)
{

    int job_post_ret;
    job_id_t i;

    gossip_debug(SERVER_DEBUG, "mkdir state: send_bmi\n");

    /* fill in response -- status field is the only generic one we should have to set */
    s_op->resp->status = ret->error_code;

    /* Encode the message */
    job_post_ret = PINT_encode(s_op->resp,
			       PINT_ENCODE_RESP,
			       &(s_op->encoded),
			       s_op->addr,
			       s_op->enc_type);

    assert(job_post_ret == 0);

#ifndef PVFS2_SERVER_DEBUG_BMI

    job_post_ret = job_bmi_send_list(
	    s_op->addr,
	    s_op->encoded.buffer_list,
	    s_op->encoded.size_list,
	    s_op->encoded.list_count,
	    s_op->encoded.total_size,
	    s_op->tag,
	    s_op->encoded.buffer_flag,
	    0,
	    s_op, 
	    ret, 
	    &i);

#else

    job_post_ret = job_bmi_send(
	    s_op->addr,
	    s_op->encoded.buffer_list[0],
	    s_op->encoded.total_size,
	    s_op->tag,
	    s_op->encoded.buffer_flag,
	    0,
	    s_op,
	    ret,
	    &i);

#endif

    return(job_post_ret);

}



/*
 * Function: mkdir_cleanup
 *
 * Params:   server_op *b, 
 *           job_status_s *ret
 *
 * Returns:  int
 *
 * Synopsis: free memory and return
 *           
 */


static int mkdir_cleanup(PINT_server_op *s_op, job_status_s *ret)
{

    PINT_encode_release(&(s_op->encoded),PINT_ENCODE_RESP,0);
    PINT_decode_release(&(s_op->decoded),PINT_DECODE_REQ,0);
    
    if(s_op->resp)
    {
	free(s_op->resp);
    }
	
    /*
    BMI_memfree(
	    s_op->addr,
	    s_op->req,
	    s_op->unexp_bmi_buff->size,
	    BMI_RECV_BUFFER
	    );
    */
    free(s_op);

    return(0);

}

/* TODO: fix comment block */
static int mkdir_error(PINT_server_op *s_op, job_status_s *ret)
{
    s_op->resp->u.mkdir.handle = 0;
    ret->error_code = -1;
    return(1);
}
/* TODO: fix comment block */
static int mkdir_critical_error(PINT_server_op *s_op, job_status_s *ret)
{
    /* make the server stop processing requests */
    return(-1);
}
/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

