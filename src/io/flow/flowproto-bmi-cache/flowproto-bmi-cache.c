/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* stubs for bmi/cache flowprotocol */

#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <limits.h>
#include <unistd.h>
#include <sys/time.h>

#include "gossip.h"
#include "flow.h"
#include "flowproto-support.h"
#include "quicklist.h"
#include "pvfs2-request.h"
#include "thread-mgr.h"
#include "pthread.h"
#include "gen-locks.h"

/**********************************************************
 * interface prototypes 
 */
int flowproto_bmi_cache_initialize(int flowproto_id);

int flowproto_bmi_cache_finalize(void);

int flowproto_bmi_cache_getinfo(flow_descriptor * flow_d,
				int option,
				void *parameter);

int flowproto_bmi_cache_setinfo(flow_descriptor * flow_d,
				int option,
				void *parameter);

int flowproto_bmi_cache_announce_flow(flow_descriptor * flow_d);

int flowproto_bmi_cache_checkworld(flow_descriptor ** flow_d_array,
				   int *count,
				   int max_idle_time_ms);

int flowproto_bmi_cache_service(flow_descriptor * flow_d);

char flowproto_bmi_cache_name[] = "flowproto_bmi_cache";

struct flowproto_ops flowproto_bmi_cache_ops = {
    flowproto_bmi_cache_name,
    flowproto_bmi_cache_initialize,
    flowproto_bmi_cache_finalize,
    flowproto_bmi_cache_getinfo,
    flowproto_bmi_cache_setinfo,
    flowproto_bmi_cache_announce_flow,
    flowproto_bmi_cache_checkworld,
    flowproto_bmi_cache_service
};

/****************************************************
 * internal types and global variables
 */

/* types of flows */
enum flow_type
{
    BMI_TO_TROVE = 1,
    TROVE_TO_BMI = 2,
    BMI_TO_MEM = 3,
    MEM_TO_BMI = 4
};

/* default buffer size to use for I/O */
static int DEFAULT_BUFFER_SIZE = 1024 * 1024 * 4;
/* our assigned flowproto id */
static int flowproto_bmi_cache_id = -1;

/* max number of discontig regions we will handle at once */
#define MAX_REGIONS 16

/* bmi context */
static bmi_context_id global_bmi_context = -1;

#define BMI_TEST_SIZE 8
/* array of bmi ops in flight; filled in when needed to call
 * testcontext() 
 */
#ifndef __PVFS2_JOB_THREADED__
static bmi_op_id_t bmi_op_array[BMI_TEST_SIZE];
static int bmi_error_code_array[BMI_TEST_SIZE];
static void* bmi_usrptr_array[BMI_TEST_SIZE];
static bmi_size_t bmi_actualsize_array[BMI_TEST_SIZE];
#endif

static int bmi_pending_count = 0;

/* queue of flows that that have either completed or need service but 
 * have not yet been passed back by flow_proto_checkXXX()
 */
static QLIST_HEAD(done_checking_queue);

#ifdef __PVFS2_JOB_THREADED__
static pthread_cond_t callback_cond = PTHREAD_COND_INITIALIZER;
static gen_mutex_t callback_mutex = GEN_MUTEX_INITIALIZER;
#endif

/* used to track completions returned by BMI callback function */
struct bmi_completion_notification
{
    void* user_ptr;
    bmi_size_t actual_size;
    bmi_error_code_t error_code;
    struct qlist_head queue_link;
};
static QLIST_HEAD(bmi_notify_queue);
static gen_mutex_t bmi_notify_mutex = GEN_MUTEX_INITIALIZER;

/* this struct contains information associated with a flow that is
 * specific to this flow protocol, this one is for mem<->bmi ops
 */
struct bmi_cache_flow_data
{
    /* type of flow */
    enum flow_type type;
    /* queue link */
    struct qlist_head queue_link;
    /* pointer to the flow that this structure is associated with */
    flow_descriptor *parent;

    /* array of memory regions for current BMI operation */
    PVFS_size bmi_size_list[MAX_REGIONS];
    PVFS_offset bmi_offset_list[MAX_REGIONS];
    void *bmi_buffer_list[MAX_REGIONS];
    PVFS_size bmi_total_size;
    int bmi_list_count;

    /* intermediate buffer to use in certain situations */
    void *intermediate_buffer;
    PVFS_size intermediate_used;

    /* id of current BMI operation */
    bmi_op_id_t bmi_id;

    /* callback information if we are in threaded mode */
    struct PINT_thread_mgr_bmi_callback bmi_callback;

    /* max size of buffers being used */
    int max_buffer_size;

    struct bmi_completion_notification bmi_notification;
};

/****************************************************
 * internal helper function declarations
 */

/* a macro to help get to our private data that is stashed in each flow
 * descriptor
 */
#define PRIVATE_FLOW(target_flow)\
	((struct bmi_cache_flow_data*)(target_flow->flow_protocol_data))

static void release_flow(flow_descriptor * flow_d);
static void buffer_teardown_bmi_to_mem(flow_descriptor * flow_d);
static void buffer_teardown_mem_to_bmi(flow_descriptor * flow_d);
static int buffer_setup_mem_to_bmi(flow_descriptor * flow_d);
static int buffer_setup_bmi_to_mem(flow_descriptor * flow_d);
static int alloc_flow_data(flow_descriptor * flow_d);
static void service_mem_to_bmi(flow_descriptor * flow_d);
static void service_bmi_to_mem(flow_descriptor * flow_d);
static flow_descriptor *bmi_completion(void *user_ptr,
				       bmi_size_t actual_size,
				       bmi_error_code_t error_code);
static void bmi_completion_bmi_to_mem(bmi_error_code_t error_code,
				      bmi_size_t actual_size,
				      flow_descriptor * flow_d);
static void bmi_completion_mem_to_bmi(bmi_error_code_t error_code,
				      flow_descriptor * flow_d);
static void bmi_callback_fn(void *user_ptr,
		         bmi_size_t actual_size,
		         bmi_error_code_t error_code);

/****************************************************
 * public interface functions
 */

/* flowproto_bmi_cache_initialize()
 *
 * initializes the flowprotocol
 *
 * returns 0 on success, -errno on failure
 */
int flowproto_bmi_cache_initialize(int flowproto_id)
{
    int ret = -1;
    int tmp_maxsize;
    gossip_ldebug(FLOW_PROTO_DEBUG, "flowproto_bmi_cache initialized.\n");

    /* make sure that the bmi interface is initialized */
    if ((ret = BMI_get_info(0, BMI_CHECK_INIT, NULL)) < 0)
    {
	return (ret);
    }

    /* make sure that the default buffer size does not exceed the
     * max buffer size allowed by BMI
     */
    if ((ret = BMI_get_info(0, BMI_CHECK_MAXSIZE, &tmp_maxsize)) < 0)
    {
	return (ret);
    }
    if (tmp_maxsize < DEFAULT_BUFFER_SIZE)
    {
	DEFAULT_BUFFER_SIZE = tmp_maxsize;
    }
    if (DEFAULT_BUFFER_SIZE < 1)
    {
	gossip_lerr("Error: BMI buffer size too small!\n");
	return (-EINVAL);
    }

#ifdef __PVFS2_JOB_THREADED__
    /* take advantage of the thread that the job interface is
     * going to use for BMI operations...
     */
    ret = PINT_thread_mgr_bmi_start();
    if(ret < 0)
    {
	return(ret);
    }
    ret = PINT_thread_mgr_bmi_getcontext(&global_bmi_context);
    /* TODO: this should never fail after a successful start */
    assert(ret == 0);

#else
    /* get a BMI context */
    ret = BMI_open_context(&global_bmi_context);
    if(ret < 0)
    {
	return(ret);
    }
#endif /* __PVFS2_JOB_THREADED__ */

    flowproto_bmi_cache_id = flowproto_id;

    return (0);
}

/* flowproto_bmi_cache_finalize()
 *
 * shuts down the flow protocol
 *
 * returns 0 on success, -errno on failure
 */
int flowproto_bmi_cache_finalize(void)
{

#ifdef __PVFS2_JOB_THREADED__
    PINT_thread_mgr_bmi_stop();
#else
    BMI_close_context(global_bmi_context);
#endif

    gossip_ldebug(FLOW_PROTO_DEBUG, "flowproto_bmi_cache shut down.\n");
    return (0);
}

/* flowproto_bmi_cache_getinfo()
 *
 * reads optional parameters from the flow protocol
 *
 * returns 0 on success, -errno on failure
 */
int flowproto_bmi_cache_getinfo(flow_descriptor * flow_d,
				int option,
				void *parameter)
{
    int* type;

    switch (option)
    {
    case FLOWPROTO_TYPE_QUERY:
	type = parameter;
	if(*type == FLOWPROTO_BMI_CACHE)
	    return(0);
	else
	    return(-ENOPROTOOPT);
    default:
	return (-ENOSYS);
	break;
    }
}

/* flowproto_bmi_cache_setinfo()
 *
 * sets optional flow protocol parameters
 *
 * returns 0 on success, -errno on failure
 */
int flowproto_bmi_cache_setinfo(flow_descriptor * flow_d,
				int option,
				void *parameter)
{
    return (-ENOSYS);
}

/* flowproto_bmi_cache_announce_flow()
 *
 * informs the flow protocol that it is responsible for the given flow
 *
 * returns 0 on success, -errno on failure
 */
int flowproto_bmi_cache_announce_flow(flow_descriptor * flow_d)
{
    int ret = -1;

    ret = alloc_flow_data(flow_d);
    if (ret < 0)
    {
	return (ret);
    }

    flow_d->flowproto_id = flowproto_bmi_cache_id;

    if (ret == 1)
    {
	/* already done! */
	flow_d->state = FLOW_COMPLETE;
    }
    else
    {
	/* we are ready for service now */
	flow_d->state = FLOW_SVC_READY;
    }

    return (0);
}

/* flowproto_bmi_cache_checkworld()
 *
 * checks to see if any previously posted flows need to be serviced
 *
 * returns 0 on success, -errno on failure
 */
int flowproto_bmi_cache_checkworld(flow_descriptor ** flow_d_array,
				   int *count,
				   int max_idle_time_ms)
{
    int ret = -1;
    flow_descriptor *active_flowd = NULL;
    struct bmi_cache_flow_data *flow_data = NULL;
    int incount = *count;
#ifdef __PVFS2_JOB_THREADED__
    struct timespec pthread_timeout;
    struct timeval base;
    int bmi_notify_queue_empty = 0;
    struct bmi_completion_notification* notification = NULL;
#else
    int split_idle_time_ms = max_idle_time_ms;
    int bmi_outcount = 0;
    int i = 0;
#endif

    /* TODO: do something more clever with the max_idle_time_ms
     * argument.  For now we just split it evenly among the
     * interfaces that we are talking to
     */

#ifdef __PVFS2_JOB_THREADED__
    /* if we are in threaded mode with callbacks, and the completion queues 
     * are empty, then we will block briefly on a condition variable 
     * to prevent busy spinning until the callback occurs
     */
gen_mutex_lock(&bmi_notify_mutex);
    bmi_notify_queue_empty = qlist_empty(&bmi_notify_queue);
gen_mutex_unlock(&bmi_notify_mutex);

    if(bmi_notify_queue_empty)
    {
	
	/* figure out how long to wait */
	gettimeofday(&base, NULL);
	pthread_timeout.tv_sec = base.tv_sec + max_idle_time_ms / 1000;
	pthread_timeout.tv_nsec = base.tv_usec * 1000 + 
	    ((max_idle_time_ms % 1000) * 1000000);
	if (pthread_timeout.tv_nsec > 1000000000)
	{
	    pthread_timeout.tv_nsec = pthread_timeout.tv_nsec - 1000000000;
	    pthread_timeout.tv_sec++;
	}

	gen_mutex_lock(&callback_mutex);
	ret = pthread_cond_timedwait(&callback_cond, &callback_mutex, &pthread_timeout);
	gen_mutex_unlock(&callback_mutex);
	if(ret != 0 && ret != ETIMEDOUT)
	{
	    /* TODO: handle this */
	    gossip_lerr("Error: unhandled pthread_cond_timedwait() failure.\n");
	    assert(0);
	}
    }

    /* handle any completed bmi operations from the notification queues */
gen_mutex_lock(&bmi_notify_mutex);
    while(!qlist_empty(&bmi_notify_queue))
    {
	notification = qlist_entry(bmi_notify_queue.next,
				struct bmi_completion_notification,
				queue_link);
	assert(notification);
	qlist_del(bmi_notify_queue.next);
	active_flowd = bmi_completion(notification->user_ptr,
	    notification->actual_size,
	    notification->error_code);
	/* put flows into done_checking_queue if needed */
	if (active_flowd->state & FLOW_FINISH_MASK ||
	    active_flowd->state == FLOW_SVC_READY)
	{
	    gossip_ldebug(FLOW_PROTO_DEBUG, "adding %p to queue.\n", active_flowd);
	    qlist_add_tail(&(PRIVATE_FLOW(active_flowd)->queue_link),
			   &done_checking_queue);
	}
    }
gen_mutex_unlock(&bmi_notify_mutex);

#else /* not __PVFS2_JOB_THREADED__ */

    if (bmi_pending_count > 0)
    {
	/* test for completion */
	ret = BMI_testcontext(BMI_TEST_SIZE, bmi_op_array, &bmi_outcount,
			   bmi_error_code_array,
			   bmi_actualsize_array, bmi_usrptr_array,
			   split_idle_time_ms, global_bmi_context);
	if (ret < 0)
	{
	    return (ret);
	}

	bmi_pending_count -= bmi_outcount;

	/* handle each completed bmi operation */
	for (i = 0; i < bmi_outcount; i++)
	{
	    active_flowd = bmi_completion(bmi_usrptr_array[i],
					  bmi_actualsize_array[i],
					  bmi_error_code_array[i]);

	    /* put flows into done_checking_queue if needed */
	    if (active_flowd->state & FLOW_FINISH_MASK ||
		active_flowd->state == FLOW_SVC_READY)
	    {
		gossip_ldebug(FLOW_PROTO_DEBUG, "adding %p to queue.\n", active_flowd);
		qlist_add_tail(&(PRIVATE_FLOW(active_flowd)->queue_link),
			       &done_checking_queue);
	    }
	}
    }
#endif

    /* collect flows out of the done_checking_queue and return */
    *count = 0;
    while (*count < incount && !qlist_empty(&done_checking_queue))
    {
	flow_data = qlist_entry(done_checking_queue.next,
				struct bmi_cache_flow_data,
				queue_link);
	active_flowd = flow_data->parent;
	gossip_ldebug(FLOW_PROTO_DEBUG, "found %p in queue.\n", active_flowd);
	qlist_del(done_checking_queue.next);
	if (active_flowd->state & FLOW_FINISH_MASK)
	{
	    release_flow(active_flowd);
	}
	flow_d_array[*count] = active_flowd;
	(*count)++;
    }
    return (0);
}

/* flowproto_bmi_cache_service()
 *
 * services a single flow descriptor
 *
 * returns 0 on success, -ERRNO on failure
 */
int flowproto_bmi_cache_service(flow_descriptor * flow_d)
{
    /* our job here is to:
     * 1) swap buffers as needed 
     * 2) post BMI and Trove operations as needed
     * we do no testing here, and only accept flows in the
     *    "FLOW_SVC_READY" state
     */
    gossip_ldebug(FLOW_PROTO_DEBUG, "flowproto_bmi_cache_service() called.\n");

    if (flow_d->state != FLOW_SVC_READY)
    {
	gossip_lerr("Error: invalid state.\n");
	return (-EINVAL);
    }

    /* handle the flow differently depending on what type it is */
    /* we don't check return values because errors are indicated by the
     * flow->state at this level
     */
    switch (PRIVATE_FLOW(flow_d)->type)
    {
    case BMI_TO_MEM:
	service_bmi_to_mem(flow_d);
	break;
    case MEM_TO_BMI:
	service_mem_to_bmi(flow_d);
	break;
    default:
	gossip_lerr("Error; unknown or unsupported endpoint pair.\n");
	return (-EINVAL);
	break;
    }

    /* clean up before returning if the flow completed */
    if (flow_d->state & FLOW_FINISH_MASK)
    {
	release_flow(flow_d);
    }

    return (0);
}

/*******************************************************
 * definitions for internal utility functions
 */

/* release_flow()
 *
 * used by protocol to reliquish control of a flow (and free any
 * internal resources associated with it) upon completion
 *
 * no return value
 */
static void release_flow(flow_descriptor * flow_d)
{
    struct bmi_cache_flow_data *tmp_data = NULL;

    assert(flow_d != NULL);

    tmp_data = PRIVATE_FLOW(flow_d);
    assert(tmp_data != NULL);

    gossip_debug(FLOW_PROTO_DEBUG, "releasing flow (start addr): %p\n",
	flow_d);
    gossip_debug(FLOW_PROTO_DEBUG, "releasing flow (end addr): %p\n",
	((char*)flow_d + sizeof(flow_descriptor)));
    gossip_debug(FLOW_PROTO_DEBUG, "releasing flow_data (start addr): %p\n",
	tmp_data);
    gossip_debug(FLOW_PROTO_DEBUG, "releasing flow_data (end addr): %p\n",
	((char*)tmp_data + sizeof(struct bmi_cache_flow_data)));
	
    switch (tmp_data->type)
    {
	case BMI_TO_MEM:
	    buffer_teardown_bmi_to_mem(flow_d);
	    break;
	case MEM_TO_BMI:
	    buffer_teardown_mem_to_bmi(flow_d);
	    break;
	default:
	    gossip_lerr("Error: Unknown/unimplemented "
			"endpoint combination.\n");
	    flow_d->state = FLOW_ERROR;
	    flow_d->error_code = -EINVAL;
	    break;
    }

    /* free flowproto data */
    free(flow_d->flow_protocol_data);
    flow_d->flow_protocol_data = NULL;
    return;
}

/* buffer_teardown_bmi_to_mem()
 *
 * cleans up buffer resources for a particular type of flow
 *
 * returns 0 on success, -errno on failure
 */
static void buffer_teardown_bmi_to_mem(flow_descriptor * flow_d)
{
    struct bmi_cache_flow_data *flow_data = PRIVATE_FLOW(flow_d);

    /* destroy any intermediate buffers */
    if (flow_data->intermediate_buffer)
    {
	BMI_memfree(flow_d->src.u.bmi.address,
		    flow_data->intermediate_buffer, flow_data->max_buffer_size,
		    BMI_RECV);
    }

    return;
}

/* buffer_teardown_mem_to_bmi()
 *
 * cleans up buffer resources for a particular type of flow
 *
 * returns 0 on success, -errno on failure
 */
static void buffer_teardown_mem_to_bmi(flow_descriptor * flow_d)
{
    struct bmi_cache_flow_data *flow_data = PRIVATE_FLOW(flow_d);

    /* destroy any intermediate buffers */
    if (flow_data->intermediate_buffer)
    {
	BMI_memfree(flow_d->dest.u.bmi.address,
		    flow_data->intermediate_buffer, flow_data->max_buffer_size,
		    BMI_SEND);
    }

    return;
}

/* buffer_setup_bmi_to_mem()
 *
 * sets up initial internal buffer configuration for a particular type
 * of flow
 *
 * returns 0 on success, -errno on failure
 */
static int buffer_setup_bmi_to_mem(flow_descriptor * flow_d)
{
    struct bmi_cache_flow_data *flow_data = PRIVATE_FLOW(flow_d);
    int ret = -1;
    int i = 0;

    /* set the buffer size to use for this flow */
    flow_data->max_buffer_size = DEFAULT_BUFFER_SIZE;

    flow_d->result.segmax = MAX_REGIONS;
    flow_d->result.bytes = 0;
    flow_d->result.segs = 0;
    ret  = PINT_Process_request(flow_d->file_req_state, flow_d->mem_req_state,
	&flow_d->file_data, &flow_d->result, PINT_CLIENT);
    if (ret < 0)
    {
	return (ret);
    }
    flow_data->bmi_list_count = flow_d->result.segs;
    flow_data->bmi_total_size = flow_d->result.bytes;

    if(PINT_REQUEST_DONE(flow_d->file_req_state) 
	&& (flow_data->bmi_total_size == 0))
    {
	/* no work to do here, return 1 to complete the flow */
	return (1);
    }

    /* did we provide enough segments to satisfy the amount of data
     * available < buffer size? 
     */
    if(!PINT_REQUEST_DONE(flow_d->file_req_state)
	&& flow_data->bmi_total_size != flow_data->max_buffer_size)
    {
	/* We aren't at the end, but we didn't get what we asked
	 * for.  In this case, we want to hang onto the segment
	 * descriptions, but provide an intermediate buffer of
	 * max_buffer_size to be able to handle the next 
	 * incoming message.  Copy out later.  
	 */
	gossip_ldebug(FLOW_PROTO_DEBUG,
		      "Warning: falling back to intermediate buffer.\n");
	if (flow_data->bmi_list_count != MAX_REGIONS)
	{
	    gossip_lerr("Error: reached an unexpected req processing state.\n");
	    return (-EINVAL);
	}
	for (i = 0; i < flow_data->bmi_list_count; i++)
	{
	    /* setup buffer list */
	    flow_data->bmi_buffer_list[i] =
		flow_d->dest.u.mem.buffer + flow_data->bmi_offset_list[i];
	}
	/* allocate an intermediate buffer if not already present */
	if (!flow_data->intermediate_buffer)
	{
	    flow_data->intermediate_buffer =
		BMI_memalloc(flow_d->src.u.bmi.address,
			     flow_data->max_buffer_size, BMI_RECV);
	    if (!flow_data->intermediate_buffer)
	    {
		return (-ENOMEM);
	    }
	}
    }
    else
    {
	/* "normal" case */
	for (i = 0; i < flow_data->bmi_list_count; i++)
	{
	    /* setup buffer list */
	    flow_data->bmi_buffer_list[i] =
		flow_d->dest.u.mem.buffer + flow_data->bmi_offset_list[i];
	}
    }

    return (0);
}

/* buffer_setup_mem_to_bmi()
 *
 * sets up initial internal buffer configuration for a particular type
 * of flow
 *
 * returns 0 on success, -errno on failure
 */
static int buffer_setup_mem_to_bmi(flow_descriptor * flow_d)
{
    struct bmi_cache_flow_data *flow_data = PRIVATE_FLOW(flow_d);
    int ret = -1;
    int i = 0;
    int intermediate_offset = 0;
    char *dest_ptr = NULL;
    char *src_ptr = NULL;
    int done_flag = 0;

    /* set the buffer size to use for this flow */
    flow_data->max_buffer_size = DEFAULT_BUFFER_SIZE;

    flow_d->result.bytemax = flow_data->max_buffer_size;
    flow_d->result.segmax = MAX_REGIONS;
    flow_d->result.bytes = 0;
    flow_d->result.segs = 0;
    ret  = PINT_Process_request(flow_d->file_req_state, flow_d->mem_req_state,
	&flow_d->file_data, &flow_d->result, PINT_CLIENT);
    if (ret < 0)
    {
	return (ret);
    }
    flow_data->bmi_list_count = flow_d->result.segs;
    flow_data->bmi_total_size = flow_d->result.bytes;

    /* did we provide enough segments to satisfy the amount of data
     * available < buffer size?
     */
    if (!PINT_REQUEST_DONE(flow_d->file_req_state) 
	&& flow_data->bmi_total_size != flow_data->max_buffer_size)
    {
	/* we aren't at the end, but we didn't get the amount of data that
	 * we asked for.  In this case, we should pack into an
	 * intermediate buffer to send with BMI, because apparently we
	 * have a lot of small segments to deal with 
	 */
	gossip_ldebug(FLOW_PROTO_DEBUG,
		      "Warning: falling back to intermediate buffer.\n");
	if (flow_data->bmi_list_count != MAX_REGIONS)
	{
	    gossip_lerr("Error: reached an unexpected req processing state.\n");
	    return (-EINVAL);
	}

	/* allocate an intermediate buffer if not already present */
	if (!flow_data->intermediate_buffer)
	{
	    flow_data->intermediate_buffer =
		BMI_memalloc(flow_d->dest.u.bmi.address,
			     flow_data->max_buffer_size, BMI_SEND);
	    if (!flow_data->intermediate_buffer)
	    {
		return (-ENOMEM);
	    }
	}

	/* now, cycle through copying a full buffer's worth of data into
	 * a contiguous intermediate buffer
	 */
	do
	{
	    for (i = 0; i < flow_data->bmi_list_count; i++)
	    {
		dest_ptr = ((char *) flow_data->intermediate_buffer +
			    intermediate_offset);
		src_ptr = ((char *) flow_d->src.u.mem.buffer +
			   flow_data->bmi_offset_list[i]);
		memcpy(dest_ptr, src_ptr, flow_data->bmi_size_list[i]);
		intermediate_offset += flow_data->bmi_size_list[i];
	    }

	    if(!PINT_REQUEST_DONE(flow_d->file_req_state)
		&& intermediate_offset < flow_data->max_buffer_size)
	    {
		flow_d->result.bytemax = flow_data->max_buffer_size -
		    intermediate_offset;
		flow_d->result.segmax = MAX_REGIONS;
		flow_d->result.bytes = 0;
		flow_d->result.segs = 0;
		ret  = PINT_Process_request(flow_d->file_req_state, 
		    flow_d->mem_req_state,
		    &flow_d->file_data, &flow_d->result, PINT_CLIENT);
		if (ret < 0)
		{
		    return (ret);
		}
		flow_data->bmi_list_count = flow_d->result.segs;
		flow_data->bmi_total_size = flow_d->result.bytes;
	    }
	    else
	    {
		done_flag = 1;
	    }

	} while (!done_flag);

	/* set pointers and such to intermediate buffer so that we send
	 * from the right place on the next bmi send operation
	 */
	flow_data->bmi_list_count = 1;
	flow_data->bmi_buffer_list[0] = flow_data->intermediate_buffer;
	flow_data->bmi_size_list[0] = intermediate_offset;
	flow_data->bmi_total_size = intermediate_offset;

    }
    else
    {
	/* setup buffer list with respect to user provided region */
	for (i = 0; i < flow_data->bmi_list_count; i++)
	{
	    /* setup buffer list */
	    flow_data->bmi_buffer_list[i] =
		flow_d->src.u.mem.buffer + flow_data->bmi_offset_list[i];
	}
    }

    return (0);
}

/* alloc_flow_data()
 *
 * fills in the part of the flow descriptor that is private to this
 * protocol
 *
 * returns 0 on success, -errno on failure
 */
static int alloc_flow_data(flow_descriptor * flow_d)
{
    struct bmi_cache_flow_data *flow_data = NULL;
    int ret = -1;

    /* allocate the structure */
    flow_data = (struct bmi_cache_flow_data *) malloc(sizeof(struct
							     bmi_cache_flow_data));
    if (!flow_data)
    {
	return (-errno);
    }
    memset(flow_data, 0, sizeof(struct bmi_cache_flow_data));
    flow_d->flow_protocol_data = flow_data;
    flow_data->parent = flow_d;
    flow_d->result.offset_array = flow_data->bmi_offset_list;
    flow_d->result.size_array = flow_data->bmi_size_list;

    /* if a file datatype offset was specified, go ahead and skip ahead 
     * before doing anything else
     */
    if(flow_d->file_req_offset)
	PINT_REQUEST_STATE_SET_TARGET(flow_d->file_req_state,
	    flow_d->file_req_offset);

    /* set boundaries on file datatype based on mem datatype or aggregate size */
    if(flow_d->aggregate_size > -1)
    {
	PINT_REQUEST_STATE_SET_FINAL(flow_d->file_req_state,
	    flow_d->aggregate_size+flow_d->file_req_offset);
    }
    else
    {
	PINT_REQUEST_STATE_SET_FINAL(flow_d->file_req_state,
	    flow_d->file_req_offset +
	    PINT_REQUEST_TOTAL_BYTES(flow_d->mem_req));
    }
    
    /* the rest of the buffer setup varies depending on the endpoints */
    if (flow_d->src.endpoint_id == BMI_ENDPOINT &&
	flow_d->dest.endpoint_id == MEM_ENDPOINT)
    {
	flow_data->type = BMI_TO_MEM;
	ret = buffer_setup_bmi_to_mem(flow_d);
    }
    else if (flow_d->src.endpoint_id == MEM_ENDPOINT &&
	     flow_d->dest.endpoint_id == BMI_ENDPOINT)
    {
	flow_data->type = MEM_TO_BMI;
	ret = buffer_setup_mem_to_bmi(flow_d);
    }
    else
    {
	gossip_lerr("Error: Unknown/unimplemented endpoint combination.\n");
	free(flow_data);
	return (-EINVAL);
    }

    if (ret < 0)
    {
	free(flow_data);
    }

    /* NOTE: we may return 1 here; the caller will catch it */
    return (ret);
}


/* service_mem_to_bmi() 
 *
 * services a particular type of flow
 *
 * no return value
 */
static void service_mem_to_bmi(flow_descriptor * flow_d)
{
    struct bmi_cache_flow_data *flow_data = PRIVATE_FLOW(flow_d);
    int ret = -1;
    enum bmi_buffer_type buffer_type;
    void* user_ptr = flow_d;

    /* make sure BMI knows if we are using an intermediate buffer or not,
     * because those have been created with bmi_memalloc()
     */
    if (flow_data->bmi_buffer_list[0] == flow_data->intermediate_buffer)
    {
	buffer_type = BMI_PRE_ALLOC;
    }
    else
    {
	buffer_type = BMI_EXT_ALLOC;
    }

    flow_data->bmi_callback.fn = bmi_callback_fn;
    flow_data->bmi_callback.data = flow_d;
#ifdef __PVFS2_JOB_THREADED__
    user_ptr = &flow_data->bmi_callback;
#endif

    /* post list send */
    gossip_ldebug(FLOW_PROTO_DEBUG, "Posting send, total size: %ld\n",
		  (long) flow_data->bmi_total_size);
    ret = BMI_post_send_list(&flow_data->bmi_id,
			     flow_d->dest.u.bmi.address,
			     (const void **) flow_data->bmi_buffer_list,
			     flow_data->bmi_size_list,
			     flow_data->bmi_list_count,
			     flow_data->bmi_total_size, buffer_type,
			     flow_d->tag, user_ptr, global_bmi_context);
    if (ret == 1)
    {
	/* handle immediate completion */
	bmi_completion_mem_to_bmi(0, flow_d);
    }
    else if (ret == 0)
    {
	/* successful post, need to test later */
	bmi_pending_count++;
	flow_d->state = FLOW_TRANSMITTING;
    }
    else
    {
	/* error posting operation */
	flow_d->state = FLOW_DEST_ERROR;
	flow_d->error_code = ret;
    }

    return;
}

/* service_bmi_to_mem() 
 *
 * services a particular type of flow
 *
 * no return value
 */
static void service_bmi_to_mem(flow_descriptor * flow_d)
{
    struct bmi_cache_flow_data *flow_data = PRIVATE_FLOW(flow_d);
    int ret = -1;
    PVFS_size actual_size = 0;
    void* user_ptr = flow_d;

    /* we should be ready to post the next operation when we get to
     * this function 
     */

    flow_data->bmi_callback.fn = bmi_callback_fn;
    flow_data->bmi_callback.data = flow_d;
#ifdef __PVFS2_JOB_THREADED__
    user_ptr = &flow_data->bmi_callback;
#endif

    /* are we using an intermediate buffer? */
    if(!PINT_REQUEST_DONE(flow_d->file_req_state)
	&& flow_data->bmi_total_size != flow_data->max_buffer_size)
    {
	gossip_ldebug(FLOW_PROTO_DEBUG,
		      "Warning: posting recv to intermediate buffer.\n");

	/* post receive to contig. intermediate buffer */
	gossip_ldebug(FLOW_PROTO_DEBUG, "Posting recv, total size: %ld\n",
		      (long) flow_data->max_buffer_size);
	ret = BMI_post_recv(&flow_data->bmi_id,
			    flow_d->src.u.bmi.address,
			    flow_data->intermediate_buffer,
			    flow_data->max_buffer_size, &actual_size,
			    BMI_PRE_ALLOC, flow_d->tag, user_ptr,
			    global_bmi_context);
    }
    else
    {
	
	if(flow_data->bmi_total_size == 0)
	{
	    gossip_lerr("WARNING: encountered odd request state; assuming flow is done.\n");
	    flow_d->state = FLOW_COMPLETE;
	    return;
	}

	/* post normal list operation */
	gossip_ldebug(FLOW_PROTO_DEBUG, "Posting recv, total size: %ld\n",
		      (long) flow_data->bmi_total_size);
	ret = BMI_post_recv_list(&flow_data->bmi_id,
				 flow_d->src.u.bmi.address,
				 flow_data->bmi_buffer_list,
				 flow_data->bmi_size_list,
				 flow_data->bmi_list_count,
				 flow_data->bmi_total_size, &actual_size,
				 BMI_EXT_ALLOC, flow_d->tag, user_ptr,
				 global_bmi_context);
    }

    gossip_ldebug(FLOW_PROTO_DEBUG, "Recv post returned %d\n", ret);

    if (ret == 1)
    {
	/* handle immediate completion */
	bmi_completion_bmi_to_mem(0, actual_size, flow_d);
    }
    else if (ret == 0)
    {
	/* successful post, need to test later */
	bmi_pending_count++;
	flow_d->state = FLOW_TRANSMITTING;
    }
    else
    {
	/* error posting operation */
	flow_d->state = FLOW_SRC_ERROR;
	flow_d->error_code = ret;
    }

    return;
}


/* bmi_callback_fn()
 *
 * callback used upon completion of BMI operations handled by the 
 * thread manager
 *
 * no return value
 */
static void bmi_callback_fn(void *user_ptr,
		         bmi_size_t actual_size,
		         bmi_error_code_t error_code)
{
    flow_descriptor* flow_d = (flow_descriptor*)user_ptr;
    struct bmi_cache_flow_data *flow_data = PRIVATE_FLOW(flow_d);

    bmi_pending_count--;

    /* add an entry to the notification queue */
    flow_data->bmi_notification.user_ptr = user_ptr;
    flow_data->bmi_notification.actual_size = actual_size;
    flow_data->bmi_notification.error_code = error_code;

gen_mutex_lock(&bmi_notify_mutex);
    qlist_add_tail(&flow_data->bmi_notification.queue_link,
	&bmi_notify_queue);
gen_mutex_unlock(&bmi_notify_mutex);

    /* wake up anyone who may be sleeping in checkworld */
#ifdef __PVFS2_JOB_THREADED__
    pthread_cond_signal(&callback_cond);
#endif
    return;
}


/* bmi_completion()
 *
 * handles completion of the specified bmi operation
 *
 * returns pointer to associated flow on success, NULL on failure
 */
static flow_descriptor *bmi_completion(void *user_ptr,
				       bmi_size_t actual_size,
				       bmi_error_code_t error_code)
				       
{
    flow_descriptor *flow_d = user_ptr;

    switch (PRIVATE_FLOW(flow_d)->type)
    {
    case BMI_TO_MEM:
	bmi_completion_bmi_to_mem(error_code, actual_size, flow_d);
	break;
    case MEM_TO_BMI:
	bmi_completion_mem_to_bmi(error_code, flow_d);
	break;
    default:
	gossip_lerr("Error: Unknown/unsupported endpoint combinantion.\n");
	flow_d->state = FLOW_ERROR;
	flow_d->error_code = -EINVAL;
	break;
    }

    return (flow_d);
}


/* bmi_completion_mem_to_bmi()
 *
 * handles the completion of bmi operations for memory to bmi transfers
 *
 * no return value
 */
static void bmi_completion_mem_to_bmi(bmi_error_code_t error_code,
				      flow_descriptor * flow_d)
{
    int ret = -1;
    struct bmi_cache_flow_data *flow_data = PRIVATE_FLOW(flow_d);

    if (error_code != 0)
    {
	/* the bmi operation failed */
	flow_d->state = FLOW_DEST_ERROR;
	flow_d->error_code = error_code;
	return;
    }

    flow_d->total_transfered += flow_data->bmi_total_size;
    gossip_ldebug(FLOW_PROTO_DEBUG, "Total completed (mem to bmi): %ld\n",
		  (long) flow_d->total_transfered);

    /* did this complete the flow? */
    if(PINT_REQUEST_DONE(flow_d->file_req_state))
    {
	flow_d->state = FLOW_COMPLETE;
	return;
    }

    /* process next portion of request */
    ret = buffer_setup_mem_to_bmi(flow_d);
    if (ret < 0)
    {
	gossip_lerr("Error: buffer_setup_mem_to_bmi() failure.\n");
	flow_d->state = FLOW_ERROR;
	flow_d->error_code = ret;
    }

    flow_d->state = FLOW_SVC_READY;
    return;
}


/* bmi_completion_bmi_to_mem()
 *
 * handles the completion of bmi operations for bmi to memory transfers
 *
 * no return value
 */
static void bmi_completion_bmi_to_mem(bmi_error_code_t error_code,
				      bmi_size_t actual_size,
				      flow_descriptor * flow_d)
{
    struct bmi_cache_flow_data *flow_data = PRIVATE_FLOW(flow_d);
    int ret = -1;
    int i = 0;
    PVFS_size total_copied = 0;
    PVFS_size region_size = 0;

    gossip_ldebug(FLOW_PROTO_DEBUG,
		  "bmi_completion_bmi_to_mem() handling error_code = %d\n",
		  (int) error_code);

    if (error_code != 0)
    {
	/* the bmi operation failed */
	flow_d->state = FLOW_SRC_ERROR;
	flow_d->error_code = error_code;
	return;
    }

    /* NOTE: lots to handle here:
     * - intermediate buffers
     * - short messages
     * - zero byte messages
     */

    /* TODO: handle case of receiving less data than we expected (or 
     * at least assert on it)
     */

    flow_d->total_transfered += actual_size;
    gossip_ldebug(FLOW_PROTO_DEBUG, "Total completed (bmi to mem): %ld\n",
		  (long) flow_d->total_transfered);

    /* were we using an intermediate buffer? */
    if(!PINT_REQUEST_DONE(flow_d->file_req_state)
	&& flow_data->bmi_total_size != flow_data->max_buffer_size)
    {
	gossip_ldebug(FLOW_PROTO_DEBUG, "copying out intermediate buffer.\n");
	/* we need to memcpy out whatever we got to the correct
	 * regions, pushing req proc further if needed
	 */
	for (i = 0; i < flow_data->bmi_list_count; i++)
	{
	    if ((total_copied + flow_data->bmi_size_list[i]) > actual_size)
	    {
		region_size = actual_size - total_copied;
	    }
	    else
	    {
		region_size = flow_data->bmi_size_list[i];
	    }
	    memcpy(flow_data->bmi_buffer_list[i],
		   ((char *) flow_data->intermediate_buffer + total_copied),
		   region_size);
	    total_copied += region_size;
	    if (total_copied == actual_size)
	    {
		break;
	    }
	}

	/* if we didn't exhaust the immediate buffer, then call
	 * request processing code until we get all the way through
	 * it
	 */
	if (total_copied < actual_size)
	{
	    if(PINT_REQUEST_DONE(flow_d->file_req_state))
	    {
		gossip_lerr
		    ("Error: Flow sent more data than could be handled?\n");
		exit(-1);
	    }

	    do
	    {
		flow_data->bmi_list_count = MAX_REGIONS;
		flow_data->bmi_total_size = actual_size - total_copied;
		flow_d->result.bytemax = flow_data->bmi_total_size;
		flow_d->result.segmax = MAX_REGIONS;
		flow_d->result.bytes = 0;
		flow_d->result.segs = 0;

		ret  = PINT_Process_request(flow_d->file_req_state, 
		    flow_d->mem_req_state,
		    &flow_d->file_data, &flow_d->result, PINT_CLIENT);
		if (ret < 0)
		{
		    flow_d->state = FLOW_DEST_ERROR;
		    flow_d->error_code = error_code;
		    return;
		}
		flow_data->bmi_list_count = flow_d->result.segs;
		flow_data->bmi_total_size = flow_d->result.bytes;

		for (i = 0; i < flow_data->bmi_list_count; i++)
		{
		    region_size = flow_data->bmi_size_list[i];
		    flow_data->bmi_buffer_list[i] =
			(char *) flow_d->dest.u.mem.buffer +
			flow_data->bmi_offset_list[i];
		    memcpy(flow_data->bmi_buffer_list[i],
			   ((char *) flow_data->intermediate_buffer +
			    total_copied), region_size);
		    total_copied += region_size;
		}
	    } while (total_copied < actual_size);
	}
    }
    else
    {   /* no intermediate buffer */
	/* TODO: fix this; we need to do something reasonable if we
	 * receive less data than we expected
	 */
	assert(actual_size == flow_data->bmi_total_size);
    }

    /* did this complete the flow? */
    if(PINT_REQUEST_DONE(flow_d->file_req_state))
    {
	flow_d->state = FLOW_COMPLETE;
	return;
    }

    /* setup next set of buffers for service */
    ret = buffer_setup_bmi_to_mem(flow_d);
    if (ret < 0)
    {
	gossip_lerr("Error: buffer_setup_bmi_to_mem() failure.\n");
	flow_d->state = FLOW_ERROR;
	flow_d->error_code = ret;
    }
    else
    {
	flow_d->state = FLOW_SVC_READY;
    }

    return;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
