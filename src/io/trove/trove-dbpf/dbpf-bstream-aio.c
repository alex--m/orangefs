/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>

#include "trove.h"
#include "trove-internal.h"
#include "dbpf.h"
#include "dbpf-bstream.h"
#include "dbpf-op-queue.h"
#include "id-generator.h"
#include "gossip.h"

/* listio_convert_equal_mem_stream_count()
 *
 * - the total size of the regions described by the memory and
 *   stream lists are equal
 *
 * Returns -1 on error, 0 if there are still pieces of the input
 * list to convert, and 1 if processing is complete.
 *
 * Stored state in lio_state so that processing can continue
 * on subsequent calls.
 */
static inline int listio_convert_equal_mem_stream_count(
    int fd,
    int op_type,
    char **mem_offset_array,
    TROVE_size *mem_size_array,
    int mem_count,
    TROVE_offset *stream_offset_array,
    TROVE_size *stream_size_array,
    int stream_count,
    struct aiocb *aiocb_array,
    int *aiocb_count_p,
    struct bstream_listio_state *lio_state)
{
    int mct, sct, act = 0;
    int cur_mem_size;
    char *cur_mem_off;
    TROVE_size cur_stream_size;
    TROVE_offset cur_stream_off;
    struct aiocb *cur_aiocb_ptr;

    if (lio_state == NULL)
    {
	mct             = 0;
	sct             = 0;
	cur_mem_size    = mem_size_array[0];
	cur_mem_off     = mem_offset_array[0];
	cur_stream_size = stream_size_array[0];
	cur_stream_off  = stream_offset_array[0];
    }
    else
    {
	mct             = lio_state->mem_ct;
	sct             = lio_state->stream_ct;
	cur_mem_size    = lio_state->cur_mem_size;
	cur_mem_off     = lio_state->cur_mem_off;
	cur_stream_size = lio_state->cur_stream_size;
	cur_stream_off  = lio_state->cur_stream_off;
    }
    cur_aiocb_ptr   = aiocb_array;

    /* _POSIX_AIO_LISTIO_MAX */

    while ((act < *aiocb_count_p) && (mct < mem_count))
    {
	/* fill in all values that are independent of which region is smaller */
	cur_aiocb_ptr->aio_fildes     = fd;
	cur_aiocb_ptr->aio_offset     = cur_stream_off;
	cur_aiocb_ptr->aio_buf        = cur_mem_off;
	cur_aiocb_ptr->aio_reqprio    = 0; /* FIX */
	cur_aiocb_ptr->aio_lio_opcode = op_type;
	cur_aiocb_ptr->aio_sigevent.sigev_notify = SIGEV_NONE;

	if (cur_mem_size == cur_stream_size)
        {
	    /* consume both mem and stream regions */
	    cur_aiocb_ptr->aio_nbytes = cur_mem_size;

	    /* update local copies of array values */
            if (++mct < mem_count)
            {
                cur_mem_size = mem_size_array[mct];
                cur_mem_off  = mem_offset_array[mct];
                cur_stream_size = stream_size_array[++sct];
                cur_stream_off  = stream_offset_array[sct];
            }
	}
	else if (cur_mem_size < cur_stream_size)
        {
	    /* consume mem region and update stream region */
	    cur_aiocb_ptr->aio_nbytes = cur_mem_size;

	    /* update local copies of array values */
            if (++mct < mem_count)
            {
                cur_stream_size -= cur_mem_size;
                cur_stream_off  += cur_mem_size;
                cur_mem_size = mem_size_array[mct];
                cur_mem_off  = mem_offset_array[mct];
            }
	}
	else /* cur_mem_size > cur_stream_size */
        {
	    /* consume stream region and update mem region */
	    cur_aiocb_ptr->aio_nbytes = cur_stream_size;

	    /* update local copies of array values */
            if (++sct < stream_count)
            {
                cur_mem_size -= cur_stream_size;
                cur_mem_off  += cur_stream_size;
                cur_stream_size = stream_size_array[sct];
                cur_stream_off  = stream_offset_array[sct];
            }
	}
	cur_aiocb_ptr = &aiocb_array[++act];
    }

    *aiocb_count_p = act; /* return the number actually used */
    
    if (mct < mem_count)
    {
	/* haven't processed all of list regions */
	if (lio_state != NULL)
        {
	    /* save state */
	    lio_state->mem_ct          = mct;
	    lio_state->stream_ct       = sct;
	    lio_state->cur_mem_size    = cur_mem_size;
	    lio_state->cur_mem_off     = cur_mem_off;
	    lio_state->cur_stream_size = cur_stream_size;
	    lio_state->cur_stream_off  = cur_stream_off;
	}
	return 0;
    }
    return 1;
}

/* listio_convert_weighted_mem_count
 *
 * - the total size of the regions described by the memory list
 *   is larger than that in the stream lists
 *
 * Returns -1 on error, 0 if there are still pieces of the input
 * list to convert, and 1 if processing is complete.
 *
 * Stored state in lio_state so that processing can continue
 * on subsequent calls.
 */
static inline int listio_convert_weighted_mem_count(
    int fd,
    int op_type,
    char **mem_offset_array,
    TROVE_size *mem_size_array,
    int mem_count,
    TROVE_offset *stream_offset_array,
    TROVE_size *stream_size_array,
    int stream_count,
    struct aiocb *aiocb_array,
    int *aiocb_count_p,
    struct bstream_listio_state *lio_state)
{
    int mct, sct, act = 0;
    int oom = 0, oos = 0;
    int cur_mem_size;
    char *cur_mem_off;
    TROVE_size cur_stream_size;
    TROVE_offset cur_stream_off;
    struct aiocb *cur_aiocb_ptr;

    if (lio_state == NULL)
    {
	mct             = 0;
	sct             = 0;
	cur_mem_size    = mem_size_array[0];
	cur_mem_off     = mem_offset_array[0];
	cur_stream_size = stream_size_array[0];
	cur_stream_off  = stream_offset_array[0];
    }
    else
    {
	mct             = lio_state->mem_ct;
	sct             = lio_state->stream_ct;
	cur_mem_size    = lio_state->cur_mem_size;
	cur_mem_off     = lio_state->cur_mem_off;
	cur_stream_size = lio_state->cur_stream_size;
	cur_stream_off  = lio_state->cur_stream_off;
    }
    cur_aiocb_ptr   = aiocb_array;

    /* _POSIX_AIO_LISTIO_MAX */

    while (act < *aiocb_count_p)
    {
	cur_aiocb_ptr->aio_fildes     = fd;
	cur_aiocb_ptr->aio_offset     = cur_stream_off;
	cur_aiocb_ptr->aio_buf        = cur_mem_off;
	cur_aiocb_ptr->aio_reqprio    = 0; /* FIX */
	cur_aiocb_ptr->aio_lio_opcode = op_type;
	cur_aiocb_ptr->aio_sigevent.sigev_notify = SIGEV_NONE;

        /*
          determine if we're either out of memory (oom) regions,
          or out of stream (oos) regions
        */
        oom = (((mct + 1) < mem_count) ? 0 : 1);
        oos = (((sct + 1) < stream_count) ? 0 : 1);

	if (cur_mem_size == cur_stream_size)
        {
	    /* consume both mem and stream regions */
	    cur_aiocb_ptr->aio_nbytes = cur_mem_size;

            if (!oom)
            {
                cur_mem_size = mem_size_array[++mct];
                cur_mem_off  = mem_offset_array[mct];
            }
            if (!oos)
            {
                cur_stream_size = stream_size_array[++sct];
                cur_stream_off  = stream_offset_array[sct];
            }
	}
	else if (cur_mem_size < cur_stream_size)
        {
	    /* consume mem region and update stream region */
	    cur_aiocb_ptr->aio_nbytes = cur_mem_size;

	    cur_stream_size -= cur_mem_size;
	    cur_stream_off  += cur_mem_size;

            if (!oom)
            {
                cur_mem_size = mem_size_array[++mct];
                cur_mem_off  = mem_offset_array[mct];
            }
	}
	else /* cur_mem_size > cur_stream_size */
        {
	    /* consume stream region and update mem region */
	    cur_aiocb_ptr->aio_nbytes = cur_stream_size;

	    cur_mem_size -= cur_stream_size;
	    cur_mem_off  += cur_stream_size;

            if (!oos)
            {
                cur_stream_size = stream_size_array[++sct];
                cur_stream_off  = stream_offset_array[sct];
            }
	}
	cur_aiocb_ptr = &aiocb_array[++act];

        if (oom || oos)
        {
            break;
        }
    }
    *aiocb_count_p = act; /* return the number actually used */

    /* until we've consumed everything we have, we're not finished */
    if (!oom || !oos)
    {
	/* haven't processed all of list regions */
	if (lio_state != NULL)
        {
	    lio_state->mem_ct          = mct;
	    lio_state->stream_ct       = sct;
	    lio_state->cur_mem_size    = cur_mem_size;
	    lio_state->cur_mem_off     = cur_mem_off;
	    lio_state->cur_stream_size = cur_stream_size;
	    lio_state->cur_stream_off  = cur_stream_off;
	}
	return 0;
    }
    return 1;
}

/* listio_convert_weighted_stream_count
 *
 * - the total size of the regions described by the stream list
 *   is larger than that in the memory lists
 *
 * Returns -1 on error, 0 if there are still pieces of the input
 * list to convert, and 1 if processing is complete.
 *
 * Stored state in lio_state so that processing can continue
 * on subsequent calls.
 */
static inline int listio_convert_weighted_stream_count(
    int fd,
    int op_type,
    char **mem_offset_array,
    TROVE_size *mem_size_array,
    int mem_count,
    TROVE_offset *stream_offset_array,
    TROVE_size *stream_size_array,
    int stream_count,
    struct aiocb *aiocb_array,
    int *aiocb_count_p,
    struct bstream_listio_state *lio_state)
{
    int mct, sct, act = 0;
    int cur_mem_size;
    char *cur_mem_off;
    TROVE_size cur_stream_size;
    TROVE_offset cur_stream_off;
    struct aiocb *cur_aiocb_ptr;

    gossip_debug(TROVE_DEBUG,"*** weighted stream count called with "
            "mcount %d | scount %d\n",mem_count,stream_count);

    if (lio_state == NULL)
    {
	mct             = 0;
	sct             = 0;
	cur_mem_size    = mem_size_array[0];
	cur_mem_off     = mem_offset_array[0];
	cur_stream_size = stream_size_array[0];
	cur_stream_off  = stream_offset_array[0];
    }
    else
    {
	mct             = lio_state->mem_ct;
	sct             = lio_state->stream_ct;
	cur_mem_size    = lio_state->cur_mem_size;
	cur_mem_off     = lio_state->cur_mem_off;
	cur_stream_size = lio_state->cur_stream_size;
	cur_stream_off  = lio_state->cur_stream_off;
    }
    cur_aiocb_ptr   = aiocb_array;

    /* _POSIX_AIO_LISTIO_MAX */

    while (act < *aiocb_count_p)
    {
	cur_aiocb_ptr->aio_fildes     = fd;
	cur_aiocb_ptr->aio_offset     = cur_stream_off;
	cur_aiocb_ptr->aio_buf        = cur_mem_off;
	cur_aiocb_ptr->aio_reqprio    = 0; /* FIX */
	cur_aiocb_ptr->aio_lio_opcode = op_type;
	cur_aiocb_ptr->aio_sigevent.sigev_notify = SIGEV_NONE;

	if (cur_mem_size == cur_stream_size)
        {
	    /* consume both mem and stream regions */
	    cur_aiocb_ptr->aio_nbytes = cur_mem_size;

	    /* update local copies of array values */
            if (++mct < mem_count)
            {
                cur_mem_size = mem_size_array[mct];
                cur_mem_off  = mem_offset_array[mct];
            }
            if (++sct < stream_count)
            {
                cur_stream_size = stream_size_array[sct];
                cur_stream_off  = stream_offset_array[sct];
            }
	}
	else if (cur_mem_size < cur_stream_size)
        {
	    /* consume mem region and update stream region */
	    cur_aiocb_ptr->aio_nbytes = cur_mem_size;

	    /* update local copies of array values */
	    cur_stream_size -= cur_mem_size;
	    cur_stream_off  += cur_mem_size;

            if (++mct < mem_count)
            {
                cur_mem_size = mem_size_array[mct];
                cur_mem_off  = mem_offset_array[mct];
            }
	}
	else /* cur_mem_size > cur_stream_size */
        {
	    /* consume stream region and update mem region */
	    cur_aiocb_ptr->aio_nbytes = cur_stream_size;

	    /* update local copies of array values */
	    cur_mem_size -= cur_stream_size;
	    cur_mem_off  += cur_stream_size;

            if (++sct < stream_count)
            {
                cur_stream_size = stream_size_array[sct];
                cur_stream_off  = stream_offset_array[sct];
            }
	}
	cur_aiocb_ptr = &aiocb_array[++act];

        if ((mct >= mem_count) || (sct >= stream_count))
        {
            break;
        }
    }

    *aiocb_count_p = act; /* return the number actually used */
    
    if ((mct < mem_count) || (sct < stream_count))
    {
	/* haven't processed all of list regions */
	if (lio_state != NULL)
        {
	    /* save state */
	    lio_state->mem_ct          = mct;
	    lio_state->stream_ct       = sct;
	    lio_state->cur_mem_size    = cur_mem_size;
	    lio_state->cur_mem_off     = cur_mem_off;
	    lio_state->cur_stream_size = cur_stream_size;
	    lio_state->cur_stream_off  = cur_stream_off;
	}
	return 0;
    }
    return 1;
}

/* dbpf_bstream_listio_convert()
 *
 * Returns -1 on error, 0 if there are still pieces of the
 * input list to convert, and 1 if processing is complete.
 *
 * Stored state in lio_state so that processing can
 * continue on subsequent calls.
 */
int dbpf_bstream_listio_convert(
    int fd,
    int op_type,
    char **mem_offset_array,
    TROVE_size *mem_size_array,
    int mem_count,
    TROVE_offset *stream_offset_array,
    TROVE_size *stream_size_array,
    int stream_count,
    struct aiocb *aiocb_array,
    int *aiocb_count_p,
    struct bstream_listio_state *lio_state)
{
    int ret = -1;

    if (mem_count == stream_count)
    {
        ret = listio_convert_equal_mem_stream_count(
            fd, op_type, mem_offset_array, mem_size_array, mem_count,
            stream_offset_array, stream_size_array, stream_count,
            aiocb_array, aiocb_count_p, lio_state);
    }
    else if (mem_count > stream_count)
    {
        ret = listio_convert_weighted_mem_count(
            fd, op_type, mem_offset_array, mem_size_array, mem_count,
            stream_offset_array, stream_size_array, stream_count,
            aiocb_array, aiocb_count_p, lio_state);
    }
    else /* if (stream_count > mem_count) */
    {
        ret = listio_convert_weighted_stream_count(
            fd, op_type, mem_offset_array, mem_size_array, mem_count,
            stream_offset_array, stream_size_array, stream_count,
            aiocb_array, aiocb_count_p, lio_state);
    }
    return ret;
}

#if 0
static void aiocb_print(struct aiocb *ptr)
{
    static char lio_write[]     = "LIO_WRITE";
    static char lio_read[]      = "LIO_READ";
    static char lio_nop[]       = "LOP_NOP";
    static char sigev_none[]    = "SIGEV_NONE";
    static char invalid_value[] = "invalid value";
    char *opcode, *sigev;

    opcode = (ptr->aio_lio_opcode == LIO_WRITE) ? lio_write :
	(ptr->aio_lio_opcode == LIO_READ) ? lio_read :
	(ptr->aio_lio_opcode == LIO_NOP) ? lio_nop : invalid_value;
    sigev = ((ptr->aio_sigevent.sigev_notify == SIGEV_NONE) ?
        sigev_none : invalid_value);

    gossip_debug(TROVE_DEBUG, "aio_fildes = %d, aio_offset = %d, "
                 "aio_buf = %x, aio_nbytes = %d, aio_reqprio = %d, "
                 "aio_lio_opcode = %s, aio_sigevent.sigev_notify = %s\n",
                 ptr->aio_fildes, (int)ptr->aio_offset,
                 (unsigned int)ptr->aio_buf, ptr->aio_nbytes,
                 ptr->aio_reqprio, opcode, sigev);
}
#endif

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */



