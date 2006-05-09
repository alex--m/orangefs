/*
 * (C) 2002 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include "dbpf-op-queue.h"
#include "gossip.h"

/* the queue that stores pending serviceable operations */
QLIST_HEAD(dbpf_op_queue);

/* lock to be obtained before manipulating dbpf_op_queue */
gen_mutex_t dbpf_op_queue_mutex = GEN_MUTEX_INITIALIZER;

static gen_mutex_t s_keyval_sync_mutex = GEN_MUTEX_INITIALIZER;
static int s_keyval_coalesce_counter = 0;
static int s_keyval_sync_queued_counter = 0;

static gen_mutex_t s_dspace_sync_mutex = GEN_MUTEX_INITIALIZER;
static int s_dspace_coalesce_counter = 0;
static int s_dspace_sync_queued_counter = 0;

static int s_high_watermark = 8;
static int s_low_watermark = 2;

#ifdef __PVFS2_TROVE_THREADED__
extern pthread_cond_t dbpf_op_incoming_cond;
#endif

/* dbpf_queued_op_put_and_dequeue()
 *
 * Assumption: we already have gotten responsibility for the op by
 * calling dbpf_queued_op_try_get() and succeeding.  This means that
 * the op will be in the OP_IN_SERVICE state.
 *
 * Remove the structure from the queue:
 * 1) lock the op queue
 * 2) remove op from queue
 * 3) unlock the op queue
 * 4) mark op as dequeued
 *
 * Note: this leaves open the possibility that someone might somehow
 * get a pointer to this operation and try to look at the state while
 * it is transitioning from OP_IN_SERVICE to OP_DEQUEUED.  However, in
 * order to do that they would need to get that pointer before we lock
 * the queue and use it afterwards, which is erroneous.  So we're not
 * going to try to protect against that.
 */
void dbpf_queued_op_put_and_dequeue(dbpf_queued_op_t *q_op_p)
{
    gen_mutex_lock(&dbpf_op_queue_mutex);
    assert((q_op_p->op.state == OP_IN_SERVICE) ||
           (q_op_p->op.state == OP_COMPLETED));
    dbpf_op_queue_remove(q_op_p);
    gen_mutex_unlock(&dbpf_op_queue_mutex);
    q_op_p->op.state = OP_DEQUEUED;
}

dbpf_op_queue_p dbpf_op_queue_new(void)
{
    struct qlist_head *tmp_queue = NULL;

    tmp_queue = (struct qlist_head *)malloc(sizeof(struct qlist_head));
    if (tmp_queue)
    {
        INIT_QLIST_HEAD(tmp_queue);
    }
    return tmp_queue;
}

void dbpf_op_queue_cleanup(dbpf_op_queue_p op_queue)
{
    dbpf_queued_op_t *cur_op = NULL;

    assert(op_queue);
    do
    {
        gen_mutex_lock(&dbpf_op_queue_mutex);
        cur_op = dbpf_op_queue_shownext(op_queue);
        if (cur_op)
        {
            dbpf_op_queue_remove(cur_op);
        }
        gen_mutex_unlock(&dbpf_op_queue_mutex);

    } while (cur_op);

    free(op_queue);
    op_queue = NULL;
    return;
}

void dbpf_op_queue_add(dbpf_op_queue_p op_queue,
                       dbpf_queued_op_t *dbpf_op)
{
    qlist_add_tail(&dbpf_op->link, op_queue);
}

void dbpf_op_queue_remove(dbpf_queued_op_t *dbpf_op)
{
    qlist_del(&dbpf_op->link);
}

int dbpf_op_queue_empty(dbpf_op_queue_p op_queue)
{
    return qlist_empty(op_queue);
}

dbpf_queued_op_t *dbpf_op_queue_shownext(dbpf_op_queue_p op_queue)
{
    dbpf_queued_op_t *next = NULL;
    while (op_queue->next != op_queue)
    {
        next = qlist_entry(op_queue->next, dbpf_queued_op_t, link);

        /* skip ops in the queue that just need to be synced */
        if(next->op.state == OP_SYNC_QUEUED) 
        {
            op_queue = op_queue->next;
        }
        else
        {
            return next;
        }
    }
    return NULL;
}

/* dbpf_queued_op_try_get()
 *
 * Given an op_id, we look up the queued op structure.  We then
 * determine if the op is already in service (being worked on by
 * another thread).  If it isn't, we mark it as in service and return
 * it.
 *
 * We're letting the caller have direct access to the queued op
 * structure for performance reasons.  They shouldn't be mucking with
 * the state or the next/prev pointers.  Perhaps some structure
 * reorganization is in order to make that more clear?
 *
 * Returns one of DBPF_QUEUED_OP_INVALID, DBPF_QUEUED_OP_BUSY, or
 * DBPF_QUEUED_OP_SUCCESS.
 */
int dbpf_queued_op_try_get(
    TROVE_op_id id,
    dbpf_queued_op_t **q_op_pp)
{
    int state = 0;
    dbpf_queued_op_t *q_op_p = NULL;

    q_op_p = id_gen_fast_lookup(id);
    if (!q_op_p)
    {
        return DBPF_QUEUED_OP_INVALID;
    }

    gen_mutex_lock(&q_op_p->mutex);
    state = q_op_p->op.state;
    if (state == OP_QUEUED)
    {
        q_op_p->op.state = OP_IN_SERVICE;
    }
    gen_mutex_unlock(&q_op_p->mutex);

    assert((state == OP_QUEUED) || (state == OP_COMPLETED));

    if (state == OP_QUEUED)
    {
	*q_op_pp = q_op_p;
	return DBPF_QUEUED_OP_SUCCESS;
    }
    return DBPF_QUEUED_OP_BUSY;
}

/* dbpf_queued_op_put()
 *
 * Given a q_op_p, we lock the queued op.  If "completed" is nonzero,
 * we mark the state of the operation as completed.  Otherwise we mark
 * it as queued.
 */
void dbpf_queued_op_put(dbpf_queued_op_t *q_op_p, int completed)
{
    gen_mutex_lock(&q_op_p->mutex);
    /* if op is already completed, never put it back to queued state */
    if (q_op_p->op.state != OP_COMPLETED)
    {
        q_op_p->op.state = (completed ? OP_COMPLETED : OP_QUEUED);
    }
    gen_mutex_unlock(&q_op_p->mutex);
}

/* dbpf_queued_op_queue()
 *
 * Gets the structure on the queue:
 * 1) lock the queue
 * 2) put the op into place
 * 3) unlock the queue
 * 4) return the id
 */
TROVE_op_id dbpf_queued_op_queue(dbpf_queued_op_t *q_op_p)
{
    TROVE_op_id tmp_id = 0;

    gen_mutex_lock(&dbpf_op_queue_mutex);

    tmp_id = dbpf_queued_op_queue_nolock(q_op_p);

    gen_mutex_unlock(&dbpf_op_queue_mutex);

    return tmp_id;
}

/* dbpf_queued_op_queue_nolock()
 *
 * same as dbpf_queued_op_queue(), but assumes dbpf_op_queue_mutex
 * already held
 */
TROVE_op_id dbpf_queued_op_queue_nolock(dbpf_queued_op_t *q_op_p)
{
    TROVE_op_id tmp_id = 0;

    dbpf_op_queue_add(&dbpf_op_queue, q_op_p);

    gen_mutex_lock(&q_op_p->mutex);
    q_op_p->op.state = OP_QUEUED;
    tmp_id = q_op_p->op.id;

    dbpf_queued_op_sync_coalesce_enqueue(q_op_p);
   
    gen_mutex_unlock(&q_op_p->mutex);

#ifdef __PVFS2_TROVE_THREADED__
    /*
      wake up our operation thread if it's sleeping to let
      it know that a new op is available for servicing
    */
    pthread_cond_signal(&dbpf_op_incoming_cond);
#endif

    return tmp_id;
}

/* dbpf_queued_op_dequeue()
 *
 * Remove the structure from the queue:
 * 1) lock the queue
 * 2) lock the op, verify it's not in use
 * 3) update all the pointers, mark the op as dequeued
 * 4) unlock the queue
 * 5) unlock the op
 */
void dbpf_queued_op_dequeue(dbpf_queued_op_t *q_op_p)
{
    gen_mutex_lock(&dbpf_op_queue_mutex);
    gen_mutex_lock(&q_op_p->mutex);

    assert(q_op_p->op.state != OP_DEQUEUED);
    assert(q_op_p->op.state != OP_IN_SERVICE);

    dbpf_op_queue_remove(q_op_p);

    q_op_p->op.state = OP_DEQUEUED;

    dbpf_queued_op_sync_coalesce_dequeue(q_op_p);

    gen_mutex_unlock(&dbpf_op_queue_mutex);
    gen_mutex_unlock(&q_op_p->mutex);
}

int dbpf_op_init_queued_or_immediate(
    struct dbpf_op * op_p,
    dbpf_queued_op_t ** q_op_pp,
    enum dbpf_op_type op_type,
    struct dbpf_collection *coll_p,
    TROVE_handle handle,
    int (* dbpf_op_svc_fn) (struct dbpf_op *),
    TROVE_ds_flags flags,
    TROVE_vtag_s *vtag,
    void *user_ptr,
    TROVE_context_id context_id,
    struct dbpf_op **op_pp)
{
    if(flags & TROVE_IMMEDIATE_COMPLETION)
    {
        DBPF_OP_INIT(*op_p,
                     op_type,
                     OP_QUEUED,
                     handle,
                     coll_p,
                     dbpf_op_svc_fn,
                     user_ptr,
                     flags,
                     context_id,
                     0);
        *op_pp = op_p;
    }
    else
    {
        /* grab a queued op structure */
        *q_op_pp = dbpf_queued_op_alloc();
        if (*q_op_pp == NULL)
        {
            return -TROVE_ENOMEM;
        }

        /* initialize all the common members */
        dbpf_queued_op_init(*q_op_pp,
                            op_type,
                            handle,
                            coll_p,
                            dbpf_op_svc_fn,
                            user_ptr,
                            flags,
                            context_id);
        *op_pp = &(*q_op_pp)->op;
    }
    return 0;
}

int dbpf_queue_or_service(
    struct dbpf_op *op_p,
    dbpf_queued_op_t *q_op_p,
    TROVE_ds_flags flags,
    TROVE_op_id *out_op_id_p)
{

    if(flags & TROVE_IMMEDIATE_COMPLETION)
    {
        *out_op_id_p = 0;
        return op_p->svc_fn(op_p);
    }
    else
    {
        *out_op_id_p = dbpf_queued_op_queue(q_op_p);
    }

    return 0;
}

static int dbpf_op_sync_get_counters(
    struct dbpf_op * op_p,
    int * sync_queued_count,
    int * coalesce_count,
    gen_mutex_t * mutex)
{
    if(DBPF_OP_IS_KEYVAL(op_p->type))
    {
        coalesce_count = &s_keyval_coalesce_counter;
        sync_queued_count = &s_keyval_sync_queued_counter;
        mutex = &s_keyval_sync_mutex;
    }
    else if(DBPF_OP_IS_DSPACE(op_p->type))
    {
        coalesce_count = &s_dspace_coalesce_counter;
        sync_queued_count = &s_dspace_sync_queued_counter;
        mutex = &s_dspace_sync_mutex;
    }
    else
    {
        gossip_lerr("Invalid op type for doing sync: %d\n", op_p->type);
        return -TROVE_EINVAL;
    }

    return 0;
}

int dbpf_queued_op_sync_coalesce_enqueue(
    dbpf_queued_op_t *qop_p)
{
    int ret;
    int * sync_queued_count;
    int * coalesce_count;
    gen_mutex_t * mutex;

    /* only perform check if operation does modifications that require sync */
    if(!DBPF_OP_DOES_SYNC(qop_p->op.type))
    {
        return 0;
    }

    /* only perform check if operation is either keyval or dspace */
    if(!DBPF_OP_IS_KEYVAL(qop_p->op.type) && 
       !DBPF_OP_IS_DSPACE(qop_p->op.type))
    {
        return 0;
    }

    /* only perform check if operation sync coalesce is enabled */
    if(qop_p->op.flags != TROVE_KEYVAL_SYNC_COALESCE ||
       qop_p->op.flags != TROVE_DSPACE_SYNC_COALESCE)
    {
        return 0;
    }

    ret = dbpf_op_sync_get_counters(&qop_p->op, sync_queued_count, 
                                    coalesce_count, mutex);
    if(ret < 0)
    {
        return ret;
    }

    gen_mutex_lock(mutex);
    (*sync_queued_count)++;
    if(*sync_queued_count >= s_low_watermark)
    {
        /* mark this queued operation as able to be coalesced when syncing */
        qop_p->stats.coalesce_sync = 1;
    }
    gen_mutex_unlock(mutex);

    return 0;
}

int dbpf_queued_op_sync_coalesce_dequeue(
    dbpf_queued_op_t *qop_p)
{
    int ret;
    int * sync_queued_count;
    int * coalesce_count;
    gen_mutex_t * mutex;

    /* only perform check if operation does modifications that require sync */
    if(!DBPF_OP_DOES_SYNC(qop_p->op.type))
    {
        return 0;
    }

    /* only perform check if operation is either keyval or dspace */
    if(!DBPF_OP_IS_KEYVAL(qop_p->op.type) &&
       !DBPF_OP_IS_DSPACE(qop_p->op.type))
    {
        return 0;
    }

    /* only perform check if operation sync coalesce is enabled */
    if(qop_p->op.flags != TROVE_KEYVAL_SYNC_COALESCE ||
       qop_p->op.flags != TROVE_DSPACE_SYNC_COALESCE)
    {
        return 0;
    }

    ret = dbpf_op_sync_get_counters(&qop_p->op, sync_queued_count, 
                                    coalesce_count, mutex);
    if(ret < 0)
    {
        return ret;
    }

    gen_mutex_lock(mutex);
    (*sync_queued_count)--;
    gen_mutex_unlock(mutex);

    return 0;
}
    
int dbpf_queued_op_sync_coalesce_db_ops(
    dbpf_queued_op_t *qop_p)
{
    int ret = 0;
    int * sync_queued_count;
    int * coalesce_count;
    gen_mutex_t * mutex;
    struct qlist_head * ready_op_link;
    dbpf_queued_op_t *ready_op;

    if(!DBPF_OP_IS_KEYVAL(qop_p->op.type) &&
       !DBPF_OP_IS_DSPACE(qop_p->op.type))
    {
        return -TROVE_ENOSYS;
    }
    
    if(!qop_p->stats.coalesce_sync)
    {
        /* do sync right now */
        if(DBPF_OP_IS_KEYVAL(qop_p->op.type))
        {
            DBPF_DB_SYNC_IF_NECESSARY((&qop_p->op),
                                      qop_p->op.coll_p->keyval_db, ret);
            if(ret < 0)
            {
                return ret;
            }
        }
        else if(DBPF_OP_IS_DSPACE(qop_p->op.type))
        {
            DBPF_DB_SYNC_IF_NECESSARY((&qop_p->op),
                                      qop_p->op.coll_p->ds_db, ret);
            if(ret < 0)
            {
                return ret;
            }
        }

        ret = dbpf_queued_op_complete(qop_p, 0, OP_COMPLETED);
        if(ret < 0)
        {
            return ret;
        }

        return 1;
    }
        
    ret = dbpf_op_sync_get_counters(&qop_p->op, sync_queued_count, 
                                    coalesce_count, mutex);
    if(ret < 0)
    {
        return ret;
    }

    gen_mutex_lock(mutex);

    if(*coalesce_count > s_high_watermark ||
       *sync_queued_count < s_low_watermark)
    {
        /* sync this operation and signal completion of the others
         * waiting to be synced
         */
        if(DBPF_OP_IS_KEYVAL(qop_p->op.type))
        {
            DBPF_DB_SYNC_IF_NECESSARY((&qop_p->op), 
                                      qop_p->op.coll_p->keyval_db, ret);
            if(ret < 0)
            {
                return ret;
            }

            ret = dbpf_queued_op_complete(qop_p, 0, OP_COMPLETED);
            if(ret < 0)
            {
                return ret;
            }

            /* move remaining ops in queue with ready-to-be-synced state
             * to completion queue
             */
            qlist_for_each(ready_op_link, &dbpf_op_queue)
            {
                ready_op = qlist_entry(ready_op_link,
                                       dbpf_queued_op_t,
                                       link);
                if(DBPF_OP_IS_KEYVAL(ready_op->op.type) &&
                   ready_op->op.state == OP_SYNC_QUEUED)
                {
                    dbpf_queued_op_sync_coalesce_dequeue(ready_op);
                    ret = dbpf_queued_op_complete(ready_op, 0, OP_COMPLETED);
                    if(ret < 0)
                    {
                        return ret;
                    }

                    (*coalesce_count)--;
                }
            }
        }
        else if(DBPF_OP_IS_DSPACE(qop_p->op.type))
        {
            DBPF_DB_SYNC_IF_NECESSARY((&qop_p->op),
                                      qop_p->op.coll_p->ds_db, ret);
            if(ret < 0)
            {
                return ret;
            }

            ret = dbpf_queued_op_complete(qop_p, 0, OP_COMPLETED);
            if(ret < 0)
            {
                return ret;
            }

            /* move remaining ops in queue with ready-to-be-synced state
             * to completion queue
             */
            qlist_for_each(ready_op_link, &dbpf_op_queue)
            {
                ready_op = qlist_entry(ready_op_link, 
                                       dbpf_queued_op_t, 
                                       link);
                if(DBPF_OP_IS_DSPACE(ready_op->op.type) &&
                   ready_op->op.state == OP_SYNC_QUEUED)
                {
                    dbpf_queued_op_dequeue(ready_op);
                    ret = dbpf_queued_op_complete(ready_op, 0, OP_COMPLETED);
                    if(ret < 0)
                    {
                        return ret;
                    }

                    (*coalesce_count)--;
                }
            }
        }

        ret = 1;
    }
    else
    {
        /* put back on the queue with SYNC_QUEUED state */
        qop_p->op.state = OP_SYNC_QUEUED;
        gen_mutex_lock(&dbpf_op_queue_mutex);
        dbpf_op_queue_add(&dbpf_op_queue, qop_p);
        gen_mutex_unlock(&dbpf_op_queue_mutex);

        (*coalesce_count)++;
        ret = 0;
    }

    gen_mutex_unlock(mutex);
    return ret;
}

extern dbpf_op_queue_p dbpf_completion_queue_array[TROVE_MAX_CONTEXTS];
extern gen_mutex_t *dbpf_completion_queue_array_mutex[TROVE_MAX_CONTEXTS];
extern pthread_cond_t dbpf_op_completed_cond;

int dbpf_queued_op_complete(dbpf_queued_op_t * qop_p,
                            TROVE_ds_state ret,
                            enum dbpf_op_state state)
{
    qop_p->state = ret;
    gen_mutex_lock(
	dbpf_completion_queue_array_mutex[qop_p->op.context_id]);
    dbpf_op_queue_add(dbpf_completion_queue_array[qop_p->op.context_id],
		      qop_p);
    gen_mutex_lock(&qop_p->mutex);
    qop_p->op.state = state;
    gen_mutex_unlock(&qop_p->mutex);
    pthread_cond_signal(&dbpf_op_completed_cond);
    gen_mutex_unlock(
	dbpf_completion_queue_array_mutex[qop_p->op.context_id]);

    return 0;
}

void dbpf_queued_op_set_sync_high_watermark(int high)
{
    s_high_watermark = high;
}

void dbpf_queued_op_set_sync_low_watermark(int low)
{
    s_low_watermark = low;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
