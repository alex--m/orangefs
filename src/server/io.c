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
#include <job-consist.h>
#include <assert.h>
#include <pvfs-distribution.h>
#include <pvfs-request.h>
#include <pint-distribution.h>
#include <pint-request.h>

static int io_init(PINT_server_op *s_op, job_status_s *ret);
static int io_get_size(PINT_server_op *s_op, job_status_s *ret);
static int io_send_ack(PINT_server_op *s_op, job_status_s *ret);
static int io_send_completion_ack(PINT_server_op *s_op, 
	job_status_s *ret);
static int io_start_flow(PINT_server_op *s_op, job_status_s *ret);
static int io_release(PINT_server_op *s_op, job_status_s *ret);
static int io_cleanup(PINT_server_op *s_op, job_status_s *ret);
void io_init_state_machine(void);

extern PINT_server_trove_keys_s Trove_Common_Keys[];

PINT_state_machine_s io_req_s = 
{
	NULL,
	"io",
	io_init_state_machine
};

/****************************************************************
 * This is the state machine for file system I/O operations (both
 * read and write)
 */

/* TODO: need some more states to do things like permissions and
 * access control?
 */

%%

machine io(init, get_size, send_positive_ack, send_negative_ack, 
	start_flow, cleanup, release, send_completion_ack)
{
	state init
	{
		run io_init;
		default => get_size;
	}

	state get_size
	{
		run io_get_size;
		success => send_positive_ack;
		default => send_negative_ack;
	}

	state send_positive_ack 
	{
		run io_send_ack;
		success => start_flow;
		default => release;
	}

	state send_negative_ack 
	{
		run io_send_ack;
		default => release;
	}

	state start_flow
	{
		run io_start_flow;
		default => send_completion_ack;
	}

	state send_completion_ack
	{
		run io_send_completion_ack;
		default => release;
	}

	state release
	{
		run io_release;
		default => cleanup;
	}

	state cleanup
	{
		run io_cleanup;
		default => init;
	}
}

%%

/*
 * Function: io_init_state_machine()
 *
 * Params:   void
 *
 * Pre:      None
 *
 * Post:     None
 *
 * Returns:  void
 *
 * Synopsis: Point the state machine at the array produced by the
 * state-comp preprocessor for request
 *           
 */
void io_init_state_machine(void)
{
	io_req_s.state_machine = io;
}

/*
 * Function: io_init()
 *
 * Params:   server_op *s_op, 
 *           job_status_s *ret
 *
 * Pre:      a properly formatted request structure.
 *
 * Post:     s_op->scheduled_id filled in.
 *            
 * Returns:  int
 *
 * Synopsis: posts the operation to the request scheduler
 *           
 */
static int io_init(PINT_server_op *s_op, job_status_s *ret)
{
	int job_post_ret;
	
	gossip_ldebug(SERVER_DEBUG, "IO: io_init() executed.\n");

	/* post a scheduler job */
	job_post_ret = job_req_sched_post(s_op->req,
												 s_op,
												 ret,
												 &(s_op->scheduled_id));

	return(job_post_ret);
}

/*
 * Function: io_get_size()
 *
 * Params:   server_op *s_op, 
 *           job_status_s *ret
 *
 * Pre:      operation has been scheduled.
 *
 * Post:     attributes have been retrieved for the trove object
 *            
 * Returns:  int
 *
 * Synopsis: performs a trove getattr operation, for the sole
 *           purpose of finding out the size of the bstream that we 
 *           intend to operate on 
 *           
 */
static int io_get_size(PINT_server_op *s_op, job_status_s *ret)
{
	int err = -1;
	job_id_t tmp_id;
	
	gossip_ldebug(SERVER_DEBUG, "IO: io_get_size() executed.\n");

	err = job_trove_dspace_getattr(
		s_op->req->u.io.fs_id,
		s_op->req->u.io.handle,
		s_op,
		ret,
		&tmp_id);
	
	return(err);
}


/*
 * Function: io_send_ack()
 *
 * Params:   server_op *s_op, 
 *           job_status_s *ret
 *
 * Pre:      error code has been set in job status for us to
 *           report to client
 *
 * Post:     response has been sent to client
 *            
 * Returns:  int
 *
 * Synopsis: fills in a response to the I/O request, encodes it,
 *           and sends it to the client via BMI.  Note that it may
 *           send either positive or negative acknowledgements.
 *           
 */
static int io_send_ack(PINT_server_op *s_op, job_status_s *ret)
{
	int err = -1;
	job_id_t tmp_id;
	
	gossip_ldebug(SERVER_DEBUG, "IO: io_send_ack() executed.\n");

	/* this is where we report the file size to the client before
	 * starting the I/O transfer, or else report an error if we
	 * failed to get the size, or failed for permission reasons
	 */
	s_op->resp->status = ret->error_code;
	s_op->resp->rsize = sizeof(struct PVFS_server_resp_s);
	s_op->resp->u.io.bstream_size = ret->ds_attr.b_size;

	err = PINT_encode(
		s_op->resp, 
		PINT_ENCODE_RESP, 
		&(s_op->encoded),
		s_op->addr,
		s_op->enc_type);

	if(err < 0)
	{
		/* critical error prior to job posting */
		gossip_lerr("Server: IO SM: PINT_encode() failure.\n");
		/* handle by setting error code in job status structure and
		 * returning 1
		 */
		ret->error_code = err;
		return(1);
	}
	else
	{
		err = job_bmi_send_list(
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
			&tmp_id);
	}
 
	return(err);
}

/*
 * Function: io_start_flow()
 *
 * Params:   server_op *s_op, 
 *           job_status_s *ret
 *
 * Pre:      all of the previous steps have succeeded, so that we
 *           are ready to actually perform the I/O
 *
 * Post:     I/O has been carried out
 *            
 * Returns:  int
 *
 * Synopsis: this is the most important part of the state machine.
 *           we setup the flow descriptor and post it in order to 
 *           carry out the data transfer
 *           
 */
static int io_start_flow(PINT_server_op *s_op, job_status_s *ret)
{
	int err = -1;
	job_id_t tmp_id;
	
	gossip_ldebug(SERVER_DEBUG, "IO: io_start_flow() executed.\n");

	s_op->flow_d = PINT_flow_alloc();
	if(!s_op->flow_d)
	{
		/* critical error, jump to next error state */
		ret->error_code = -ENOMEM;
		return(1);
	}

	s_op->flow_d->file_data =
		(PINT_Request_file_data*)malloc(sizeof(PINT_Request_file_data));
	if(!s_op->flow_d->file_data)
	{
		/* critical error, jump to next error state */
		ret->error_code = -ENOMEM;
		return(1);
	}

	/* we still have the file size stored in the response structure 
	 * that we sent in the previous state, other details come from
	 * request
	 */
	s_op->flow_d->file_data->fsize = s_op->resp->u.io.bstream_size;
	s_op->flow_d->file_data->dist = s_op->req->u.io.io_dist;
	s_op->flow_d->file_data->iod_num = s_op->req->u.io.iod_num;
	s_op->flow_d->file_data->iod_count = s_op->req->u.io.iod_count;

	/* on writes, we allow the bstream to be extended at EOF */
	if(s_op->req->u.io.io_type == PVFS_IO_WRITE)
		s_op->flow_d->file_data->extend_flag = 1;
	else
		s_op->flow_d->file_data->extend_flag = 0;

	s_op->flow_d->request = s_op->req->u.io.io_req;
	s_op->flow_d->flags = 0;
	s_op->flow_d->tag = s_op->tag;
	s_op->flow_d->user_ptr = NULL;

	/* set endpoints depending on type of io requested */
	if(s_op->req->u.io.io_type == PVFS_IO_WRITE)
	{
		s_op->flow_d->src.endpoint_id = BMI_ENDPOINT;
		s_op->flow_d->src.u.bmi.address = s_op->addr;
		s_op->flow_d->dest.endpoint_id = TROVE_ENDPOINT;
		s_op->flow_d->dest.u.trove.handle = s_op->req->u.io.handle;
		s_op->flow_d->dest.u.trove.coll_id = s_op->req->u.io.fs_id;
	}
	else if(s_op->req->u.io.io_type == PVFS_IO_READ)
	{
		s_op->flow_d->src.endpoint_id = TROVE_ENDPOINT;
		s_op->flow_d->src.u.trove.handle = s_op->req->u.io.handle;
		s_op->flow_d->src.u.trove.coll_id = s_op->req->u.io.fs_id;
		s_op->flow_d->dest.endpoint_id = BMI_ENDPOINT;
		s_op->flow_d->dest.u.bmi.address = s_op->addr;
	}
	else
	{
		/* critical error; jump out of state */
		gossip_lerr("Server: IO SM: unknown IO type requested.\n");
		ret->error_code = -EINVAL;
		return(1);
	}


	/* start the flow! */
	err = job_flow(
		s_op->flow_d,
		s_op,
		ret,
		&tmp_id);

	return(err);
}


/*
 * Function: io_release()
 *
 * Params:   server_op *b, 
 *           job_status_s *ret
 *
 * Pre:      we are done with all steps necessary to service
 *           request
 *
 * Post:     operation has been released from the scheduler
 *
 * Returns:  int
 *
 * Synopsis: releases the operation from the scheduler
 */
static int io_release(PINT_server_op *s_op, job_status_s *ret)
{

	int job_post_ret=0;
	job_id_t i;

	gossip_ldebug(SERVER_DEBUG, "IO: io_release() executed.\n");

	/* tell the scheduler that we are done with this operation */
	job_post_ret = job_req_sched_release(s_op->scheduled_id,
													  s_op,
													  ret,
													  &i);
	return job_post_ret;
}

/*
 * Function: io_cleanup()
 *
 * Params:   server_op *b, 
 *           job_status_s *ret
 *
 * Pre:      all jobs done, simply need to clean up
 *
 * Post:     everything is free
 *
 * Returns:  int
 *
 * Synopsis: free up any buffers associated with the operation,
 *           including any encoded or decoded protocol structures
 */
static int io_cleanup(PINT_server_op *s_op, job_status_s *ret)
{
	gossip_ldebug(SERVER_DEBUG, "IO: io_cleanup() executed.\n");
	/* Free the encoded message if necessary! */

	if(s_op->flow_d)
	{
		if(s_op->flow_d->file_data)
		{
			free(s_op->flow_d->file_data);
		}
		PINT_flow_free(s_op->flow_d);
	}

	/* let go of our encoded response buffer, 
	 * if we appear to have made one 
	 */
	if(s_op->encoded.total_size)
	{
		PINT_encode_release(&s_op->encoded, PINT_ENCODE_RESP,
			s_op->enc_type);
	}

	/* let go of our non-encoded response buffer */
	if(s_op->resp)
	{
		free(s_op->resp);
	}

	/* let go of our decoded request buffer */
	if(s_op->decoded.buffer)
	{
		PINT_decode_release(
			&s_op->decoded,
			PINT_ENCODE_REQ,
			s_op->enc_type);
	}

	/* let go of original, non-decoded request buffer */
	free(s_op->unexp_bmi_buff.buffer);

	free(s_op);

#ifndef DO_NOT_DEBUG_SERVER_OPS
    return(899);
#endif

	return(0);
}

/*
 * Function: io_send_completion_ack()
 *
 * Params:   server_op *s_op, 
 *           job_status_s *ret
 *
 * Pre:      flow is completed so that we can report its status
 *
 * Post:     if this is a write, response has been sent to client
 *           if this is a read, do nothing
 *            
 * Returns:  int
 *
 * Synopsis: fills in a response to the I/O request, encodes it,
 *           and sends it to the client via BMI.  Note that it may
 *           send either positive or negative acknowledgements.
 *           
 */
static int io_send_completion_ack(PINT_server_op *s_op, 
	job_status_s *ret)
{
	int err = -1;
	job_id_t tmp_id;
	
	gossip_ldebug(SERVER_DEBUG, "IO: io_send_completion_ack() executed.\n");

	/* we only send this trailing ack if we are working on a write
	 * operation; otherwise just cut out early
	 */
	if(s_op->req->u.io.io_type == PVFS_IO_READ)
	{
		ret->error_code = 0;
		return(1);
	}

	/* fill in response -- status field is the only generic one we should have to set */
	s_op->resp->status = ret->error_code;
	s_op->resp->u.write_completion.total_completed = s_op->flow_d->total_transfered;

	err = PINT_encode(
		s_op->resp, 
		PINT_ENCODE_RESP, 
		&(s_op->encoded),
		s_op->addr,
		s_op->enc_type);

	if(err < 0)
	{
		/* critical error prior to job posting */
		gossip_lerr("Server: IO SM: PINT_encode() failure.\n");
		/* handle by setting error code in job status structure and
		 * returning 1
		 */
		ret->error_code = err;
		return(1);
	}
	else
	{
		err = job_bmi_send_list(
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
			&tmp_id);
	}
 
	return(err);
}
