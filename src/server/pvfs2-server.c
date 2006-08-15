/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <assert.h>
#include <getopt.h>

#ifdef __PVFS2_SEGV_BACKTRACE__
#include <execinfo.h>
#define __USE_GNU
#include <ucontext.h>
#endif

#include "bmi.h"
#include "gossip.h"
#include "job.h"
#include "trove.h"
#include "pvfs2-debug.h"
#include "pvfs2-storage.h"
#include "PINT-reqproto-encode.h"
#include "src/server/request-scheduler/request-scheduler.h"
#include "pvfs2-server.h"
#include "state-machine-fns.h"
#include "mkspace.h"
#include "server-config.h"
#include "quicklist.h"
#include "pint-dist-utils.h"
#include "pint-perf-counter.h"
#include "pint-event.h"
#include "id-generator.h"
#include "job-time-mgr.h"
#include "pint-cached-config.h"
#include "pvfs2-internal.h"

#ifndef PVFS2_VERSION
#define PVFS2_VERSION "Unknown"
#endif

#ifdef __PVFS2_TROVE_THREADED__
#ifdef __PVFS2_TROVE_AIO_THREADED__
#define SERVER_STORAGE_MODE "aio-threaded"
#else
#define SERVER_STORAGE_MODE "threaded"
#endif
#else
#define SERVER_STORAGE_MODE "non-threaded"
#endif

#define PVFS2_VERSION_REQUEST 0xFF

/* this controls how many jobs we will test for per job_testcontext()
 * call. NOTE: this is currently independent of the config file
 * parameter that governs how many unexpected BMI jobs are kept posted
 * at any given time
 */
#define PVFS_SERVER_TEST_COUNT 64

/* track performance counters for the server */
static struct PINT_perf_key server_keys[] =
{
    {"bytes read", PINT_PERF_READ, 0},
    {"bytes written", PINT_PERF_WRITE, 0},
    {"metadata reads", PINT_PERF_METADATA_READ, PINT_PERF_PRESERVE},
    {"metadata writes", PINT_PERF_METADATA_WRITE, PINT_PERF_PRESERVE},
    {"metadata dspace ops", PINT_PERF_METADATA_DSPACE_OPS, PINT_PERF_PRESERVE},
    {"metadata keyval ops", PINT_PERF_METADATA_KEYVAL_OPS, PINT_PERF_PRESERVE},
    {"request scheduler", PINT_PERF_REQSCHED, PINT_PERF_PRESERVE},
    {NULL, 0, 0},
};

/* For the switch statement to know what interfaces to shutdown */
static PINT_server_status_flag server_status_flag;

/* All parameters read in from the configuration file */
static struct server_configuration_s server_config;

/* A flag to stop the main loop from processing and handle the signal
 * after all threads complete and are no longer blocking.
 */
static int signal_recvd_flag = 0;
static pid_t server_controlling_pid = 0;

/* A list of all serv_op's posted for unexpected message alone */
static QLIST_HEAD(posted_sop_list);
/* A list of all serv_op's posted for expected messages alone */
static QLIST_HEAD(inprogress_sop_list);

/* this is used externally by some server state machines */
job_context_id server_job_context = -1;

typedef struct
{
    int server_remove_storage_space;
    int server_create_storage_space;
    int server_background;
    char *pidfile;
} options_t;

static options_t s_server_options = { 0, 0, 1, NULL };

/* each of the elements in this array consists of a string and its length.
 * we're able to use sizeof here because sizeof an inlined string ("") gives
 * the length of the string with the null terminator
 */
PINT_server_trove_keys_s Trove_Common_Keys[] =
{
    {ROOT_HANDLE_KEYSTR, sizeof(ROOT_HANDLE_KEYSTR)},
    {DIRECTORY_ENTRY_KEYSTR, sizeof(DIRECTORY_ENTRY_KEYSTR)},
    {DATAFILE_HANDLES_KEYSTR, sizeof(DATAFILE_HANDLES_KEYSTR)},
    {METAFILE_DIST_KEYSTR, sizeof(METAFILE_DIST_KEYSTR)},
    {SYMLINK_TARGET_KEYSTR, sizeof(SYMLINK_TARGET_KEYSTR)}
};

/* extended attribute name spaces supported in PVFS2 */
const char *PINT_eattr_namespaces[] =
{
    "system.",
    "user.",
    "trusted.",
    "security.",
    NULL
};

/* These three are used continuously in our wait loop.  They could be
 * relatively large, so rather than allocate them on the stack, we'll
 * make them dynamically allocated globals.
 */
static job_id_t *server_job_id_array = NULL;
static void **server_completed_job_p_array = NULL;
static job_status_s *server_job_status_array = NULL;

/* Prototypes for internal functions */
static int server_initialize(
    PINT_server_status_flag *server_status_flag,
    job_status_s *job_status_structs);
static int server_initialize_subsystems(
    PINT_server_status_flag *server_status_flag);
static int server_setup_signal_handlers(void);
static int server_purge_unexpected_recv_machines(void);
static int server_setup_process_environment(int background);
static int server_shutdown(
    PINT_server_status_flag status,
    int ret, int sig);
static void server_sig_handler(int sig);
static int server_post_unexpected_recv(job_status_s * js_p);
static int server_parse_cmd_line_args(int argc, char **argv);
static int server_state_machine_start(
    PINT_server_op *s_op, job_status_s *js_p);
#ifdef __PVFS2_SEGV_BACKTRACE__
static void bt_sighandler(int sig, siginfo_t *info, void *secret);
#endif
static int create_pidfile(char *pidfile);
static void write_pidfile(int fd);
static void remove_pidfile(void);
static int parse_port_from_host_id(char* host_id);

/* table of incoming request types and associated parameters */
struct PINT_server_req_params PINT_server_req_table[] =
{
    /* 0 */
    {PVFS_SERV_INVALID,
        "invalid",
        PINT_SERVER_CHECK_INVALID,
        PINT_SERVER_ATTRIBS_REQUIRED,
        NULL},

    /* 1 */
    {PVFS_SERV_CREATE,
        "create",
        PINT_SERVER_CHECK_NONE,
        PINT_SERVER_ATTRIBS_REQUIRED,
        &pvfs2_create_sm},

    /* 2 */
    {PVFS_SERV_REMOVE,
        "remove",
        PINT_SERVER_CHECK_NONE,
        PINT_SERVER_ATTRIBS_NOT_REQUIRED,
        &pvfs2_remove_sm},

    /* 3 */
    {PVFS_SERV_IO,
        "io",
        PINT_SERVER_CHECK_NONE,
        PINT_SERVER_ATTRIBS_NOT_REQUIRED,
        &pvfs2_io_sm},

    /* 4 */
    {PVFS_SERV_GETATTR,
        "getattr",
        PINT_SERVER_CHECK_ATTR,
        PINT_SERVER_ATTRIBS_NOT_REQUIRED,
        &pvfs2_get_attr_sm},

    /* 5 */
    {PVFS_SERV_SETATTR,
        "setattr",
        PINT_SERVER_CHECK_ATTR,
        PINT_SERVER_ATTRIBS_NOT_REQUIRED,
        &pvfs2_set_attr_sm},

    /* 6 */
    {PVFS_SERV_LOOKUP_PATH,
        "lookup_path",
        PINT_SERVER_CHECK_NONE,
        PINT_SERVER_ATTRIBS_REQUIRED,
        &pvfs2_lookup_sm},

    /* 7 */
    {PVFS_SERV_CRDIRENT,
        "crdirent",
        PINT_SERVER_CHECK_CRDIRENT,
        PINT_SERVER_ATTRIBS_REQUIRED,
        &pvfs2_crdirent_sm},

    /* 8 */
    {PVFS_SERV_RMDIRENT,
        "rmdirent",
        PINT_SERVER_CHECK_WRITE,
        PINT_SERVER_ATTRIBS_REQUIRED,
        &pvfs2_rmdirent_sm},

    /* 9 */
    {PVFS_SERV_CHDIRENT,
        "chdirent",
        PINT_SERVER_CHECK_WRITE,
        PINT_SERVER_ATTRIBS_REQUIRED,
        &pvfs2_chdirent_sm},

    /* 10 */
    {PVFS_SERV_TRUNCATE,
        "truncate",
        PINT_SERVER_CHECK_NONE,
        PINT_SERVER_ATTRIBS_NOT_REQUIRED,
        &pvfs2_truncate_sm},

    /* 11 */
    {PVFS_SERV_MKDIR,
        "mkdir",
        PINT_SERVER_CHECK_NONE,
        PINT_SERVER_ATTRIBS_REQUIRED,
        &pvfs2_mkdir_sm},

    /* 12 */
    {PVFS_SERV_READDIR,
        "readdir",
        PINT_SERVER_CHECK_READ,
        PINT_SERVER_ATTRIBS_REQUIRED,
        &pvfs2_readdir_sm},

    /* 13 */
    {PVFS_SERV_GETCONFIG,
        "getconfig",
        PINT_SERVER_CHECK_NONE,
        PINT_SERVER_ATTRIBS_REQUIRED,
        &pvfs2_get_config_sm},

    /* 14 */
    {PVFS_SERV_WRITE_COMPLETION,
        "write_completion",
        PINT_SERVER_CHECK_INVALID,
        PINT_SERVER_ATTRIBS_REQUIRED,
        NULL},

    /* 15 */
    {PVFS_SERV_FLUSH,
        "flush",
        PINT_SERVER_CHECK_NONE,
        PINT_SERVER_ATTRIBS_NOT_REQUIRED,
        &pvfs2_flush_sm},

    /* 16 */
    {PVFS_SERV_MGMT_SETPARAM,
        "mgmt_setparam",
        PINT_SERVER_CHECK_NONE,
        PINT_SERVER_ATTRIBS_REQUIRED,
        &pvfs2_setparam_sm},

    /* 17 */
    {PVFS_SERV_MGMT_NOOP,
        "mgmt_noop",
        PINT_SERVER_CHECK_NONE,
        PINT_SERVER_ATTRIBS_REQUIRED,
        &pvfs2_noop_sm},

    /* 18 */
    {PVFS_SERV_STATFS,
        "statfs",
        PINT_SERVER_CHECK_NONE,
        PINT_SERVER_ATTRIBS_REQUIRED,
        &pvfs2_statfs_sm},

    /* 19 */
    {PVFS_SERV_PERF_UPDATE,
        "perf_update",
        PINT_SERVER_CHECK_INVALID,
        PINT_SERVER_ATTRIBS_REQUIRED,
        &pvfs2_perf_update_sm},

    /* 20 */
    {PVFS_SERV_MGMT_PERF_MON,
        "mgmt_perf_mon",
        PINT_SERVER_CHECK_NONE,
        PINT_SERVER_ATTRIBS_REQUIRED,
        &pvfs2_perf_mon_sm},

    /* 21 */
    {PVFS_SERV_MGMT_ITERATE_HANDLES,
        "mgmt_iterate_handles",
        PINT_SERVER_CHECK_NONE,
        PINT_SERVER_ATTRIBS_REQUIRED,
        &pvfs2_iterate_handles_sm},

    /* 22 */
    {PVFS_SERV_MGMT_DSPACE_INFO_LIST,
        "mgmt_dspace_info_list",
        PINT_SERVER_CHECK_NONE,
        PINT_SERVER_ATTRIBS_REQUIRED,
        NULL},

    /* 23 */
    {PVFS_SERV_MGMT_EVENT_MON,
        "mgmt_event_mon",
        PINT_SERVER_CHECK_NONE,
        PINT_SERVER_ATTRIBS_REQUIRED,
        &pvfs2_event_mon_sm},

    /* 24 */
    {PVFS_SERV_MGMT_REMOVE_OBJECT,
        "mgmt-remove-object",
        PINT_SERVER_CHECK_NONE,
        PINT_SERVER_ATTRIBS_NOT_REQUIRED,
        &pvfs2_mgmt_remove_object_sm},

    /* 25 */
    {PVFS_SERV_MGMT_REMOVE_DIRENT,
        "mgmt-remove-dirent",
        PINT_SERVER_CHECK_NONE,
        PINT_SERVER_ATTRIBS_NOT_REQUIRED,
        &pvfs2_mgmt_remove_dirent_sm},

    /* 26 */
    {PVFS_SERV_MGMT_GET_DIRDATA_HANDLE,
        "mgmt-get-dirdata-handle",
        PINT_SERVER_CHECK_NONE,
        PINT_SERVER_ATTRIBS_NOT_REQUIRED,
        &pvfs2_mgmt_get_dirdata_handle_sm},

    /* 27 */
    {PVFS_SERV_JOB_TIMER,
        "job_timer",
        PINT_SERVER_CHECK_INVALID,
        PINT_SERVER_ATTRIBS_REQUIRED,
        &pvfs2_job_timer_sm},

    /* 28 */
    {PVFS_SERV_PROTO_ERROR,
        "proto_error",
        PINT_SERVER_CHECK_INVALID,
        PINT_SERVER_ATTRIBS_REQUIRED,
        &pvfs2_proto_error_sm},

    /* 29 */
    {PVFS_SERV_GETEATTR,
        "geteattr",
        PINT_SERVER_CHECK_ATTR,
        PINT_SERVER_ATTRIBS_NOT_REQUIRED,
        &pvfs2_get_eattr_sm},

    /* 30 */
    {PVFS_SERV_SETEATTR,
        "seteattr",
        PINT_SERVER_CHECK_ATTR,
        PINT_SERVER_ATTRIBS_NOT_REQUIRED,
        &pvfs2_set_eattr_sm},

    /* 31 */
    {PVFS_SERV_DELEATTR,
        "deleattr",
        PINT_SERVER_CHECK_ATTR,
        PINT_SERVER_ATTRIBS_NOT_REQUIRED,
        &pvfs2_del_eattr_sm},

    /* 32 */
    {PVFS_SERV_LISTEATTR,
        "listeattr",
        PINT_SERVER_CHECK_ATTR,
        PINT_SERVER_ATTRIBS_NOT_REQUIRED,
        &pvfs2_list_eattr_sm},

    /* 33 */
    {PVFS_SERV_SMALL_IO,
        "small_io",
        PINT_SERVER_CHECK_NONE,
        PINT_SERVER_ATTRIBS_NOT_REQUIRED,
        &pvfs2_small_io_sm}
};

int main(int argc, char **argv)
{
    int ret = -1, siglevel = 0;
    char *fs_conf = NULL, *server_conf = NULL;
    PINT_server_op *tmp_op = NULL;
    uint64_t debug_mask = 0;

#ifdef WITH_MTRACE
    mtrace();
#endif

    /* Passed to server shutdown function */
    server_status_flag = SERVER_DEFAULT_INIT;

    /* Enable the gossip interface to send out stderr and set an
     * initial debug mask so that we can output errors at startup.
     */
    gossip_enable_stderr();
    gossip_set_debug_mask(1, GOSSIP_SERVER_DEBUG);

    server_status_flag |= SERVER_GOSSIP_INIT;

    /* Determine initial server configuration, looking at both command
     * line arguments and the configuration file.
     */
    ret = server_parse_cmd_line_args(argc, argv);
    if (ret == PVFS2_VERSION_REQUEST)
    {
        return 0;
    }
    else if (ret != 0)
    {
        goto server_shutdown;
    }

    gossip_debug(GOSSIP_SERVER_DEBUG,
                 "PVFS2 Server version %s starting.\n", PVFS2_VERSION);

    fs_conf = ((argc >= optind) ? argv[optind] : NULL);
    server_conf = ((argc >= (optind + 1)) ? argv[optind + 1] : NULL);

    ret = PINT_parse_config(&server_config, fs_conf, server_conf);
    if (ret < 0)
    {
        gossip_err("Error: Please check your config files.\n");  
        gossip_err("Error: Server aborting.\n");
        goto server_shutdown;
    }

    server_status_flag |= SERVER_CONFIG_INIT;

    if (!PINT_config_is_valid_configuration(&server_config))
    {
        gossip_err("Error: Invalid configuration; aborting.\n");
        ret = -PVFS_EINVAL;
        goto server_shutdown;
    }

    /* reset gossip debug mask based on configuration settings */
    debug_mask = PVFS_debug_eventlog_to_mask(server_config.event_logging);
    gossip_set_debug_mask(1, debug_mask);
    gossip_set_logstamp(server_config.logstamp_type);
    gossip_debug(GOSSIP_SERVER_DEBUG,"Logging %s (mask %llu)\n",
                 server_config.event_logging, llu(debug_mask));

    /* remove storage space and exit if requested */
    if (s_server_options.server_remove_storage_space)
    {
        ret = PINT_config_pvfs2_rmspace(&server_config);
        if(ret < 0)
        {
            goto server_shutdown;
        }
        gossip_set_debug_mask(1, GOSSIP_SERVER_DEBUG);
        gossip_debug(GOSSIP_SERVER_DEBUG, "PVFS2 Server: storage space removed. Exiting.\n");
        gossip_set_debug_mask(1, debug_mask);
        return(0);
    }

    /* create storage space and exit if requested */
    if (s_server_options.server_create_storage_space)
    {
        ret = PINT_config_pvfs2_mkspace(&server_config);
        if(ret < 0)
        {
            goto server_shutdown;
        }
        gossip_set_debug_mask(1, GOSSIP_SERVER_DEBUG);
        gossip_debug(GOSSIP_SERVER_DEBUG, "PVFS2 Server: storage space created. Exiting.\n");
        gossip_set_debug_mask(1, debug_mask);
        return(0);
    }

    server_job_id_array = (job_id_t *)
        malloc(PVFS_SERVER_TEST_COUNT * sizeof(job_id_t));
    server_completed_job_p_array = (void **)
        malloc(PVFS_SERVER_TEST_COUNT * sizeof(void *));
    server_job_status_array = (job_status_s *)
        malloc(PVFS_SERVER_TEST_COUNT * sizeof(job_status_s));

    if (!server_job_id_array ||
        !server_completed_job_p_array ||
        !server_job_status_array)
    {
        if (server_job_id_array)
        {
            free(server_job_id_array);
            server_job_id_array = NULL;
        }
        if (server_completed_job_p_array)
        {
            free(server_completed_job_p_array);
            server_completed_job_p_array = NULL;
        }
        if (server_job_status_array)
        {
            free(server_job_status_array);
            server_job_status_array = NULL;
        }
        gossip_err("Error: failed to allocate arrays for "
                   "tracking completed jobs.\n");
        goto server_shutdown;
    }
    server_status_flag |= SERVER_JOB_OBJS_ALLOCATED;

    /* Initialize the server (many many steps) */
    ret = server_initialize(&server_status_flag, server_job_status_array);
    if (ret < 0)
    {
        gossip_err("Error: Could not initialize server; aborting.\n");
        goto server_shutdown;
    }

#ifndef __PVFS2_DISABLE_PERF_COUNTERS__
    /* kick off performance update state machine */
    ret = server_state_machine_alloc_noreq(PVFS_SERV_PERF_UPDATE,
        &(tmp_op));
    if (ret == 0)
    {
        ret = server_state_machine_start_noreq(tmp_op);
    }
    if (ret < 0)
    {
        PVFS_perror_gossip("Error: failed to start perf update "
                    "state machine.\n", ret);
        goto server_shutdown;
    }
#endif

    /* kick off timer for expired jobs */
    ret = server_state_machine_alloc_noreq(
        PVFS_SERV_JOB_TIMER, &(tmp_op));
    if (ret == 0)
    {
        ret = server_state_machine_start_noreq(tmp_op);
    }
    if (ret < 0)
    {
        PVFS_perror_gossip("Error: failed to start job timer "
                           "state machine.\n", ret);
        goto server_shutdown;
    }

    /* Initialization complete; process server requests indefinitely. */
    for ( ;; )  
    {
        int i, comp_ct = PVFS_SERVER_TEST_COUNT;

        /* IF a signal was received and we have drained all the state machines
         * that were in progress, then we initiate shutdown of the server
         */
        if (signal_recvd_flag != 0)
        {
            /*
             * If we received a signal, then find out if we can exit now
             * by checking if all s_ops (for expected messages) have either 
             * finished or timed out,
             */
            if (qlist_empty(&inprogress_sop_list))
            {
                ret = 0;
                siglevel = signal_recvd_flag;
                goto server_shutdown;
            }
            /* not completed. continue... */
        }

        ret = job_testcontext(server_job_id_array,
                              &comp_ct,
                              server_completed_job_p_array,
                              server_job_status_array,
                              PVFS2_SERVER_DEFAULT_TIMEOUT_MS,
                              server_job_context);
        if (ret < 0)
        {
            gossip_lerr("pvfs2-server panic; main loop aborting\n");
            goto server_shutdown;
        }

        /*
          Loop through the completed jobs and handle whatever comes
          next
        */
        for (i = 0; i < comp_ct; i++)
        {
            int unexpected_msg = 0;
            PINT_server_op *s_op = server_completed_job_p_array[i];

            /* Completed jobs might be ongoing, or might be new
             * (unexpected) ones.  We handle the first step of either
             * type here.
             */
            if (s_op->op == BMI_UNEXPECTED_OP)
            {
                unexpected_msg = 1;
                memset(&server_job_status_array[i], 0,
                       sizeof(job_status_s));
                ret = server_state_machine_start(
                    s_op, &server_job_status_array[i]);
                if (ret < 0)
                {
                    PVFS_perror_gossip("Error: server_state_machine_start", ret);
                    free(s_op->unexp_bmi_buff.buffer);
                    /* TODO: tell BMI to drop this address? */
                    /* set return code to zero to allow server to continue
                     * processing 
                     */
                    ret = 0;
                }
            }
            else
            {
                /* NOTE: PINT_state_machine_next() is a function that
                 * is shared with the client-side state machine
                 * processing, so it is defined in the src/common
                 * directory.
                 */
                ret = PINT_state_machine_next(
                    s_op, &server_job_status_array[i]);
            }

            /* Either of the above might have completed immediately
             * (ret == 1).  While the job continues to complete
             * immediately, we continue to service it.
             */
            while (ret == 1)
            {
                ret = PINT_state_machine_next(
                    s_op, &server_job_status_array[i]);
            }

            if (ret < 0)
            {
                PVFS_perror_gossip("Error: state machine processing error", ret);
                ret = 0;
            }

            if (unexpected_msg)
            {
                /* If this was a new (unexpected) job, we need to post
                 * a replacement unexpected job so that we can
                 * continue to receive incoming requests.
                 */
                ret = server_post_unexpected_recv(
                    &server_job_status_array[i]);
                if (ret < 0)
                {
                    /* TODO: do something here, the return value was
                     * not being checked for failure before.  I just
                     * put something here to make it exit for the
                     * moment.  -Phil
                     */
                    gossip_lerr("Error: post unexpected failure not handled.\n");
                    goto server_shutdown;
                }
            }
        }
    }

  server_shutdown:
    server_shutdown(server_status_flag, ret, siglevel);
    /* NOTE: the server_shutdown() function does not return; it always ends
     * by calling exit.  This point in the code should never be reached.
     */
    return -1;
}

/*
 * Manipulate the pid file.  Don't bother returning an error in
 * the write stage, since there's nothing that can be done about it.
 */
static int create_pidfile(char *pidfile)
{
    return open(pidfile, (O_CREAT | O_WRONLY | O_TRUNC), 0644);
}

static void write_pidfile(int fd)
{
    pid_t pid = getpid();
    char pid_str[16] = {0};
    int len;

    snprintf(pid_str, 16, "%d\n", pid);
    len = strlen(pid_str);
    write(fd, pid_str, len);
}

static void remove_pidfile(void)
{
    assert(s_server_options.pidfile);
    unlink(s_server_options.pidfile);
}

/* server_initialize()
 *
 * Handles:
 * - backgrounding, redirecting logging
 * - initializing all the subsystems (BMI, Trove, etc.)
 * - setting up the state table used to map new requests to
 *   state machines
 * - allocating and posting the initial unexpected message jobs
 * - setting up signal handlers
 */
static int server_initialize(
    PINT_server_status_flag *server_status_flag,
    job_status_s *job_status_structs)
{
    int ret = 0, i = 0; 
    FILE *dummy;
    uint64_t debug_mask = 0;
    
    assert(server_config.logfile != NULL);
    dummy = fopen(server_config.logfile, "a");
    if (dummy == NULL)
    {
        int tmp_errno = errno;
        gossip_err("error opening log file %s\n",
                server_config.logfile);
        return -tmp_errno;
    }
    fclose(dummy);

    /* redirect gossip to specified target if backgrounded */
    if (s_server_options.server_background)
    {
        freopen("/dev/null", "r", stdin);
        freopen("/dev/null", "w", stdout);
        freopen("/dev/null", "w", stderr);

        ret = gossip_enable_file(server_config.logfile, "a");
        if (ret < 0)
        {
            int tmp_errno = errno;
            gossip_lerr("error opening log file %s\n",
                        server_config.logfile);
            return -tmp_errno;
        }
        /* log starting message again so it appears in log file, not just
         * console
         */
        gossip_set_debug_mask(1, GOSSIP_SERVER_DEBUG);
        gossip_debug(GOSSIP_SERVER_DEBUG,
           "PVFS2 Server version %s starting.\n", PVFS2_VERSION);
        debug_mask = PVFS_debug_eventlog_to_mask(server_config.event_logging);
        gossip_set_debug_mask(1, debug_mask);
    }

    /* handle backgrounding, setting up working directory, and so on. */
    ret = server_setup_process_environment(
        s_server_options.server_background);
    if (ret < 0)
    {
        gossip_err("Error: Could not start server; aborting.\n");
        return ret;
    }

    /* Initialize the bmi, flow, trove and job interfaces */
    ret = server_initialize_subsystems(server_status_flag);
    if (ret < 0)
    {
        gossip_err("Error: Could not initialize server interfaces; "
                   "aborting.\n");
        return ret;
    }

    *server_status_flag |= SERVER_STATE_MACHINE_INIT;

    /* Post starting set of BMI unexpected msg buffers */
    for (i = 0; i < server_config.initial_unexpected_requests; i++)
    {
        ret = server_post_unexpected_recv(&job_status_structs[i]);
        if (ret < 0)
        {
            gossip_err("Error posting unexpected recv\n");
            return ret;
        }
    }

    *server_status_flag |= SERVER_BMI_UNEXP_POST_INIT;

    ret = server_setup_signal_handlers();

    *server_status_flag |= SERVER_SIGNAL_HANDLER_INIT;

    gossip_debug(GOSSIP_SERVER_DEBUG,
                 "Initialization completed successfully.\n");

    return ret;
}

/* server_setup_process_environment()
 *
 * performs normal daemon initialization steps
 *
 * returns 0 on success, -PVFS_EINVAL on failure (details will be logged to
 * gossip)
 */
static int server_setup_process_environment(int background)
{
    pid_t new_pid = 0;
    int pid_fd = -1;

    /*
     * Manage a pid file if requested (for init scripts).  Create
     * the file in the parent before the chdir, but let the child
     * write his pid and delete it when exiting.
     */
    if (s_server_options.pidfile)
    {
        pid_fd = create_pidfile(s_server_options.pidfile);
        if (pid_fd < 0)
        {
            gossip_err("Failed to create pid file %s: %s\n",
                       s_server_options.pidfile, strerror(errno));
            return(-PVFS_EINVAL);
        }
    }

    if (chdir("/"))
    {
        gossip_lerr("cannot change working directory to \"/\" "
                    "(errno = %x). aborting.\n", errno);
        return(-PVFS_EINVAL);
    }

    umask(0077);

    if (background)
    {
        new_pid = fork();
        if (new_pid < 0)
        {
            gossip_lerr("error in fork() system call (errno = %x). "
                        "aborting.\n", errno);
            return(-PVFS_EINVAL);
        }
        else if (new_pid > 0)
        {
            /* exit parent */
            exit(0);
        }

        new_pid = setsid();
        if (new_pid < 0)
        {
            gossip_lerr("error in setsid() system call.  aborting.\n");
            return(-PVFS_EINVAL);
        }
    }
    if (pid_fd >= 0)
    {
        write_pidfile(pid_fd);
        close(pid_fd);
        atexit(remove_pidfile);
    }
    server_controlling_pid = getpid();
    return 0;
}

/* server_initialize_subsystems()
 *
 * This:
 * - initializes distribution subsystem
 * - initializes encoding/decoding subsystem
 * - initializes BMI
 * - initializes Trove
 *   - finds the collection IDs for all file systems
 *   - gets a context from Trove
 *   - tells Trove what handles are to be used
 *     for each file system (collection)
 * - initializes the flow subsystem
 * - initializes the job subsystem
 *   - gets a job context
 * - initialize the request scheduler
 */
static int server_initialize_subsystems(
    PINT_server_status_flag *server_status_flag)
{
    int ret = -PVFS_EINVAL;
    char *method_name = NULL;
    char *cur_merged_handle_range = NULL;
    PINT_llist *cur = NULL;
    struct filesystem_configuration_s *cur_fs;
    TROVE_context_id trove_context = -1;
    char buf[16] = {0};
    PVFS_fs_id orig_fsid;
    PVFS_ds_flags init_flags = 0;
    int port_num = 0;

    /* Initialize distributions */
    ret = PINT_dist_initialize(0);
    if (ret < 0)
    {
        gossip_err("Error initializing distribution interface.\n");
        return ret;
    }
    *server_status_flag |= SERVER_DIST_INIT;

    ret = PINT_encode_initialize();
    if (ret < 0)
    {
        gossip_err("PINT_encode_initialize() failed.\n");
        return ret;
    }

    *server_status_flag |= SERVER_ENCODER_INIT;

    gossip_debug(GOSSIP_SERVER_DEBUG,
                 "Passing %s as BMI listen address.\n",
                 server_config.host_id);

    ret = BMI_initialize(server_config.bmi_modules, 
                         server_config.host_id, BMI_INIT_SERVER);
    if (ret < 0)
    {
        PVFS_perror_gossip("Error: BMI_initialize", ret);
        return ret;
    }
#ifdef USE_TRUSTED
    /* Pass the server_config file pointer to the lower
     * levels for the trusted connections related functions to be
     * called
     */
    BMI_set_info(0, BMI_TRUSTED_CONNECTION, (void *) &server_config);
    gossip_debug(GOSSIP_SERVER_DEBUG, "Enabling trusted connections!\n");
#endif
    *server_status_flag |= SERVER_BMI_INIT;

    ret = trove_collection_setinfo(0, 0, TROVE_DB_CACHE_SIZE_BYTES,
                                   &server_config.db_cache_size_bytes);
    /* this should never fail */
    assert(ret == 0);
    ret = trove_collection_setinfo(0, 0, TROVE_ALT_AIO_MODE,
        &server_config.trove_alt_aio_mode);
    /* this should never fail */
    assert(ret == 0);
    ret = trove_collection_setinfo(0, 0, TROVE_MAX_CONCURRENT_IO,
        &server_config.trove_max_concurrent_io);
    /* this should never fail */
    assert(ret == 0);

    /* parse port number and allow trove to use it to help differentiate
     * shmem regions if needed
     */
    port_num = parse_port_from_host_id(server_config.host_id);
    if(port_num > 0)
    {
        ret = trove_collection_setinfo(0, 0, TROVE_SHM_KEY_HINT, &port_num);
        assert(ret == 0);
    }

    if(server_config.db_cache_type && (!strcmp(server_config.db_cache_type,
                                               "mmap")))
    {
        /* set db cache type to mmap rather than sys */
        init_flags |= TROVE_DB_CACHE_MMAP;
    }

    /* Set the buffer size according to configuration file */
    BMI_set_info(0, BMI_TCP_BUFFER_SEND_SIZE, 
                 (void *)&server_config.tcp_buffer_size_send);
    BMI_set_info(0, BMI_TCP_BUFFER_RECEIVE_SIZE, 
                 (void *)&server_config.tcp_buffer_size_receive);

    ret = trove_initialize(server_config.storage_path,
                           init_flags, &method_name, 0);
    if (ret < 0)
    {
        PVFS_perror_gossip("Error: trove_initialize", ret);

        gossip_err("\n***********************************************\n");
        gossip_err("Invalid Storage Space: %s\n\n",
                   server_config.storage_path);
        gossip_err("Storage initialization failed.  The most "
                   "common reason\nfor this is that the storage space "
                   "has not yet been\ncreated or is located on a "
                   "partition that has not yet\nbeen mounted.  "
                   "If you'd like to create the storage space,\n"
                   "re-run this program with a -f option.\n");
        gossip_err("\n***********************************************\n");
        return ret;
    }

    *server_status_flag |= SERVER_TROVE_INIT;

    ret = PINT_cached_config_initialize();
    if(ret < 0)
    {
        gossip_err("Error initializing cached_config interface.\n");
        return(ret);
    }

    /* initialize the flow interface */
    ret = PINT_flow_initialize(server_config.flow_modules, 0);

    if (ret < 0)
    {
        PVFS_perror_gossip("Error: PINT_flow_initialize", ret);
        return ret;
    }

    *server_status_flag |= SERVER_FLOW_INIT;

    cur = server_config.file_systems;
    while(cur)
    {
        cur_fs = PINT_llist_head(cur);
        if (!cur_fs)
        {
            break;
        }

        ret = PINT_handle_load_mapping(&server_config, cur_fs);
        if(ret)
        {
            PVFS_perror("Error: PINT_handle_load_mapping", ret);
            return(ret);
        }

        orig_fsid = cur_fs->coll_id;
        ret = trove_collection_lookup(
            cur_fs->file_system_name, &(cur_fs->coll_id), NULL, NULL);

        if (ret < 0)
        {
            gossip_lerr("Error initializing filesystem %s\n",
                        cur_fs->file_system_name);
            return ret;
        }

        if(orig_fsid != cur_fs->coll_id)
        {
            gossip_err("Error: configuration file does not match storage collection.\n");
            gossip_err("   config file fs_id: %d\n", (int)orig_fsid);
            gossip_err("   storage fs_id: %d\n", (int)cur_fs->coll_id);
            gossip_err("Warning: This most likely means that the configuration\n");
            gossip_err("   files have been regenerated without destroying and\n");
            gossip_err("   recreating the corresponding storage collection.\n");
            return(-PVFS_ENODEV);
        }

        /*
         * get a range string that combines all handles for both meta
         * and data ranges specified in the config file.
         *
         * the server isn't concerned with what allocation of handles
         * are meta and which are data at this level, so we lump them
         * all together and hand them to trove-handle-mgmt.
         */
        cur_merged_handle_range =
            PINT_config_get_merged_handle_range_str(
                &server_config, cur_fs);

        /*
         * error out if we're not configured to house either a meta or
         * data handle range at all.
         */
        if (!cur_merged_handle_range)
        {
            gossip_lerr("Error: Invalid handle range for host %s "
                        "(alias %s) specified in file system %s\n",
                        server_config.host_id,
                        PINT_config_get_host_alias_ptr(
                            &server_config, server_config.host_id),
                        cur_fs->file_system_name);
            return -1;
        }
        else
        {
            ret = trove_open_context(cur_fs->coll_id, &trove_context);
            if (ret < 0)
            {
                gossip_lerr("Error initializing trove context\n");
                return ret;
            }

            /*
              set storage hints if any.  if any of these fail, we
              can't error out since they're just hints.  thus, we
              complain in logging and continue.
            */
            ret = trove_collection_setinfo(
                cur_fs->coll_id, trove_context, 
                TROVE_COLLECTION_HANDLE_TIMEOUT,
                (void *)&cur_fs->handle_recycle_timeout_sec);
            if (ret < 0)
            {
                gossip_lerr("Error setting handle timeout\n");
            }

            if (cur_fs->attr_cache_keywords &&
                cur_fs->attr_cache_size &&
                cur_fs->attr_cache_max_num_elems)
            {
                ret = trove_collection_setinfo(
                    cur_fs->coll_id, trove_context, 
                    TROVE_COLLECTION_ATTR_CACHE_KEYWORDS,
                    (void *)cur_fs->attr_cache_keywords);
                if (ret < 0)
                {
                    gossip_lerr("Error setting attr cache keywords\n");
                }
                ret = trove_collection_setinfo(
                    cur_fs->coll_id, trove_context, 
                    TROVE_COLLECTION_ATTR_CACHE_SIZE,
                    (void *)&cur_fs->attr_cache_size);
                if (ret < 0)
                {
                    gossip_lerr("Error setting attr cache size\n");
                }
                ret = trove_collection_setinfo(
                    cur_fs->coll_id, trove_context, 
                    TROVE_COLLECTION_ATTR_CACHE_MAX_NUM_ELEMS,
                    (void *)&cur_fs->attr_cache_max_num_elems);
                if (ret < 0)
                {
                    gossip_lerr("Error setting attr cache max num elems\n");
                }
                ret = trove_collection_setinfo(
                    cur_fs->coll_id, trove_context, 
                    TROVE_COLLECTION_ATTR_CACHE_INITIALIZE,
                    (void *)0);
                if (ret < 0)
                {
                    gossip_lerr("Error initializing the attr cache\n");
                }
            }

            /*
              add configured merged handle range for this host/fs.
              NOTE: if the attr cache was properly configured above,
              this next setinfo may have the opportunity to cache
              a number of attributes on startup during an iterate.
            */
            ret = trove_collection_setinfo(
                cur_fs->coll_id, trove_context,
                TROVE_COLLECTION_HANDLE_RANGES,
                (void *)cur_merged_handle_range);
            if (ret < 0)
            {
                gossip_lerr("Error adding handle range %s to "
                            "filesystem %s\n",
                            cur_merged_handle_range,
                            cur_fs->file_system_name);
                return ret;
            }

            ret = trove_collection_setinfo(
                cur_fs->coll_id, trove_context,
                TROVE_COLLECTION_COALESCING_HIGH_WATERMARK,
                (void *)&cur_fs->coalescing_high_watermark);
            if(ret < 0)
            {
                gossip_lerr("Error setting coalescing high watermark\n");
                return ret;
            }

            ret = trove_collection_setinfo(
                cur_fs->coll_id, trove_context,
                TROVE_COLLECTION_COALESCING_LOW_WATERMARK,
                (void *)&cur_fs->coalescing_low_watermark);
            if(ret < 0)
            {
                gossip_lerr("Error setting coalescing low watermark\n");
                return ret;
            }
            
            ret = trove_collection_setinfo(
                cur_fs->coll_id, trove_context,
                TROVE_COLLECTION_META_SYNC_MODE,
                (void *)&cur_fs->trove_sync_meta);
            if(ret < 0)
            {
                gossip_lerr("Error setting coalescing low watermark\n");
                return ret;
            } 
            
            ret = trove_collection_setinfo(
                cur_fs->coll_id, trove_context,
                TROVE_COLLECTION_IMMEDIATE_COMPLETION,
                (void *)&cur_fs->immediate_completion);
            if(ret < 0)
            {
                gossip_lerr("Error setting trove immediate completion\n");
                return ret;
            } 

            gossip_debug(GOSSIP_SERVER_DEBUG, "File system %s using "
                         "handles:\n\t%s\n", cur_fs->file_system_name,
                         cur_merged_handle_range);

            gossip_debug(GOSSIP_SERVER_DEBUG, "Sync on metadata update "
                         "for %s: %s\n", cur_fs->file_system_name,
                         ((cur_fs->trove_sync_meta == TROVE_SYNC) ?
                          "yes" : "no"));

            gossip_debug(GOSSIP_SERVER_DEBUG, "Sync on I/O data update "
                         "for %s: %s\n", cur_fs->file_system_name,
                         ((cur_fs->trove_sync_data == TROVE_SYNC) ?
                          "yes" : "no"));

            /* format and pass sync mode to the flow implementation */
            snprintf(buf, 16, "%d,%d", cur_fs->coll_id,
                     cur_fs->trove_sync_data);
            PINT_flow_setinfo(NULL, FLOWPROTO_DATA_SYNC_MODE, buf);

            trove_close_context(cur_fs->coll_id, trove_context);
            free(cur_merged_handle_range);
        }

        cur = PINT_llist_next(cur);
    }

    *server_status_flag |= SERVER_CACHED_CONFIG_INIT;

    gossip_debug(GOSSIP_SERVER_DEBUG,
                 "Storage Init Complete (%s)\n", SERVER_STORAGE_MODE);
    gossip_debug(GOSSIP_SERVER_DEBUG, "%d filesystem(s) initialized\n",
                 PINT_llist_count(server_config.file_systems));

    ret = job_time_mgr_init();
    if(ret < 0)
    {
        PVFS_perror_gossip("Error: job_time_mgr_init", ret);
        return(ret);
    }

    *server_status_flag |= SERVER_JOB_TIME_MGR_INIT;

    /* initialize Job Interface */
    ret = job_initialize(0);
    if (ret < 0)
    {
        PVFS_perror_gossip("Error: job_initialize", ret);
        return ret;
    }

    *server_status_flag |= SERVER_JOB_INIT;
    
    ret = job_open_context(&server_job_context);
    if (ret < 0)
    {
        gossip_err("Error opening job context.\n");
        return ret;
    }

    *server_status_flag |= SERVER_JOB_CTX_INIT;

    ret = PINT_req_sched_initialize();
    if (ret < 0)
    {
        PVFS_perror_gossip("Error: PINT_req_sched_intialize", ret);
        return ret;
    }
    *server_status_flag |= SERVER_REQ_SCHED_INIT;

#ifndef __PVFS2_DISABLE_PERF_COUNTERS__
    PINT_server_pc = PINT_perf_initialize(server_keys);
    if(!PINT_server_pc)
    {
        gossip_err("Error initializing performance counters.\n");
        return(ret);
    }
    *server_status_flag |= SERVER_PERF_COUNTER_INIT;
#endif

    ret = PINT_event_initialize(PINT_EVENT_DEFAULT_RING_SIZE);
    if (ret < 0)
    {
        gossip_err("Error initializing event interface.\n");
        return (ret);
    }
    *server_status_flag |= SERVER_EVENT_INIT;

    return ret;
}

static int server_setup_signal_handlers(void)
{
    struct sigaction new_action;
    struct sigaction ign_action;
#ifdef __PVFS2_SEGV_BACKTRACE__
    struct sigaction segv_action;

    segv_action.sa_sigaction = (void *)bt_sighandler;
    sigemptyset (&segv_action.sa_mask);
    segv_action.sa_flags = SA_RESTART | SA_SIGINFO | SA_ONESHOT;
#endif

    /* Set up the structure to specify the new action. */
    new_action.sa_handler = server_sig_handler;
    sigemptyset (&new_action.sa_mask);
    new_action.sa_flags = 0;

    ign_action.sa_handler = SIG_IGN;
    sigemptyset (&ign_action.sa_mask);
    ign_action.sa_flags = 0;

    /* catch these */
    sigaction (SIGILL, &new_action, NULL);
    sigaction (SIGTERM, &new_action, NULL);
    sigaction (SIGHUP, &new_action, NULL);
    sigaction (SIGINT, &new_action, NULL);
    sigaction (SIGQUIT, &new_action, NULL);
#ifdef __PVFS2_SEGV_BACKTRACE__
    sigaction (SIGSEGV, &segv_action, NULL);
#else
    sigaction (SIGSEGV, &new_action, NULL);
#endif

    /* ignore these */
    sigaction (SIGPIPE, &ign_action, NULL);
    sigaction (SIGUSR1, &ign_action, NULL);
    sigaction (SIGUSR2, &ign_action, NULL);

    return 0;
}

#ifdef __PVFS2_SEGV_BACKTRACE__

#if defined(REG_EIP)
#  define REG_INSTRUCTION_POINTER REG_EIP
#elif defined(REG_RIP)
#  define REG_INSTRUCTION_POINTER REG_RIP
#else
#  error Unknown instruction pointer location for your architecture, configure without --enable-segv-backtrace.
#endif

/* bt_signalhandler()
 *
 * prints a stack trace from a signal handler; code taken from a Linux
 * Journal article
 *
 * no return value
 */
static void bt_sighandler(int sig, siginfo_t *info, void *secret)
{
    void *trace[16];
    char **messages = (char **)NULL;
    int i, trace_size = 0;
    ucontext_t *uc = (ucontext_t *)secret;

    /* Do something useful with siginfo_t */
    if (sig == SIGSEGV)
    {
        gossip_err("PVFS2 server: signal %d, faulty address is %p, " 
            "from %p\n", sig, info->si_addr, 
            (void*)uc->uc_mcontext.gregs[REG_INSTRUCTION_POINTER]);
    }
    else
    {
        gossip_err("PVFS2 server: signal %d\n", sig);
    }

    trace_size = backtrace(trace, 16);
    /* overwrite sigaction with caller's address */
    trace[1] = (void *) uc->uc_mcontext.gregs[REG_INSTRUCTION_POINTER];

    messages = backtrace_symbols(trace, trace_size);
    /* skip first stack frame (points here) */
    for (i=1; i<trace_size; ++i)
        gossip_err("[bt] %s\n", messages[i]);

    signal_recvd_flag = sig;
    return;
}
#endif

static int server_shutdown(
    PINT_server_status_flag status,
    int ret, int siglevel)
{
    if (siglevel == SIGSEGV)
    {
        gossip_err("SIGSEGV: skipping cleanup; exit now!\n");
        exit(EXIT_FAILURE);
    }

    gossip_debug(GOSSIP_SERVER_DEBUG,
                 "*** server shutdown in progress ***\n");

    if (status & SERVER_STATE_MACHINE_INIT)
    {
        gossip_debug(GOSSIP_SERVER_DEBUG, "[+] halting state machine "
                     "processor   [   ...   ]\n");
        PINT_state_machine_halt();
        gossip_debug(GOSSIP_SERVER_DEBUG, "[-]         state machine "
                     "processor   [ stopped ]\n");
    }

    if (status & SERVER_CACHED_CONFIG_INIT)
    {
        gossip_debug(GOSSIP_SERVER_DEBUG, "[+] halting cached "
                     "config interface   [   ...   ]\n");
        PINT_cached_config_finalize();
        gossip_debug(GOSSIP_SERVER_DEBUG, "[-]         cached "
                     "config interface   [ stopped ]\n");
    }

    if (status & SERVER_EVENT_INIT)
    {
        gossip_debug(GOSSIP_SERVER_DEBUG, "[+] halting event "
                     "profiling interface [   ...   ]\n");
        PINT_event_finalize();
        gossip_debug(GOSSIP_SERVER_DEBUG, "[-]         event "
                     "profiling interface [ stopped ]\n");
    }

    if (status & SERVER_REQ_SCHED_INIT)
    {
        gossip_debug(GOSSIP_SERVER_DEBUG, "[+] halting request "
                     "scheduler         [   ...   ]\n");
        PINT_req_sched_finalize();
        gossip_debug(GOSSIP_SERVER_DEBUG, "[-]         request "
                     "scheduler         [ stopped ]\n");
    }
        
    if (status & SERVER_JOB_CTX_INIT)
    {
        job_close_context(server_job_context);
    }

    if (status & SERVER_JOB_INIT)
    {
        gossip_debug(GOSSIP_SERVER_DEBUG, "[+] halting job "
                     "interface             [   ...   ]\n");
        job_finalize();
        gossip_debug(GOSSIP_SERVER_DEBUG, "[-]         job "
                     "interface             [ stopped ]\n");
    }

    if (status & SERVER_JOB_TIME_MGR_INIT)
    {
        gossip_debug(GOSSIP_SERVER_DEBUG, "[+] halting job time "
                     "mgr interface    [   ...   ]\n");
        job_time_mgr_finalize();
        gossip_debug(GOSSIP_SERVER_DEBUG, "[-]         job time "
                     "mgr interface    [ stopped ]\n");
    }

    if (status & SERVER_FLOW_INIT)
    {
        gossip_debug(GOSSIP_SERVER_DEBUG, "[+] halting flow "
                     "interface            [   ...   ]\n");
        PINT_flow_finalize();
        gossip_debug(GOSSIP_SERVER_DEBUG, "[-]         flow "
                     "interface            [ stopped ]\n");
    }

    if (status & SERVER_BMI_INIT)
    {
        gossip_debug(GOSSIP_SERVER_DEBUG, "[+] halting bmi "
                     "interface             [   ...   ]\n");
        BMI_finalize();
        gossip_debug(GOSSIP_SERVER_DEBUG, "[-]         bmi "
                     "interface             [ stopped ]\n");
    }

    if (status & SERVER_TROVE_INIT)
    {
        gossip_debug(GOSSIP_SERVER_DEBUG, "[+] halting storage "
                     "interface         [   ...   ]\n");
        trove_finalize();
        gossip_debug(GOSSIP_SERVER_DEBUG, "[-]         storage "
                     "interface         [ stopped ]\n");
    }

    if (status & SERVER_ENCODER_INIT)
    {
        gossip_debug(GOSSIP_SERVER_DEBUG, "[+] halting encoder "
                     "interface         [   ...   ]\n");
        PINT_encode_finalize();
        gossip_debug(GOSSIP_SERVER_DEBUG, "[-]         encoder "
                     "interface         [ stopped ]\n");
    }

    if (status & SERVER_PERF_COUNTER_INIT)
    {
        gossip_debug(GOSSIP_SERVER_DEBUG, "[+] halting performance "
                     "interface     [   ...   ]\n");
        PINT_perf_finalize(PINT_server_pc);
        gossip_debug(GOSSIP_SERVER_DEBUG, "[-]         performance "
                     "interface     [ stopped ]\n");
    }

    if (status & SERVER_GOSSIP_INIT)
    {
        gossip_debug(GOSSIP_SERVER_DEBUG,
                     "[*] halting logging interface\n");
        gossip_disable();
    }

    if (status & SERVER_CONFIG_INIT)
    {
        PINT_config_release(&server_config);
    }

    if (status & SERVER_JOB_OBJS_ALLOCATED)
    {
        free(server_job_id_array);
        free(server_completed_job_p_array);
        free(server_job_status_array);
    }

    if(siglevel == 0 && ret != 0)
    {
        exit(EXIT_FAILURE);
    }
    exit(EXIT_SUCCESS);
}

static void server_sig_handler(int sig)
{
    struct sigaction new_action;

    if (getpid() == server_controlling_pid)
    {
        if (sig != SIGSEGV)
        {
            gossip_err("\nPVFS2 server got signal %d "
                       "(server_status_flag: %d)\n",
                       sig, (int)server_status_flag);
        }

        if (sig == SIGHUP)
        {
            gossip_err("SIGHUP: pvfs2-server cannot restart; "
                       "shutting down instead.\n");
        }

        /* ignore further invocations of this signal */
        new_action.sa_handler = SIG_IGN;
        sigemptyset(&new_action.sa_mask);
        new_action.sa_flags = 0;
        sigaction (sig, &new_action, NULL);

        /* set the signal_recvd_flag on critical errors to cause the
         * server to exit gracefully on the next work cycle
         */
        signal_recvd_flag = sig;
        /*
         * iterate through all the machines that we had posted for
         * unexpected BMI messages and deallocate them.
         * From now the server will only try and finish operations
         * that are already in progress, wait for them to timeout
         * or complete before initiating shutdown
         */
        server_purge_unexpected_recv_machines();
    }
}

static void usage(int argc, char **argv)
{
    gossip_err("Usage: %s: [OPTIONS] <global_config_file> "
               "<server_config_file>\n\n", argv[0]);
    gossip_err("  -d, --foreground\t"
               "will keep server in the foreground\n");
    gossip_err("  -f, --mkfs\t\twill cause server to "
               "create file system storage and exit\n");
    gossip_err("  -h, --help\t\twill show this message\n");
    gossip_err("  -r, --rmfs\t\twill cause server to "
               "remove file system storage and exit\n");
    gossip_err("  -v, --version\t\toutput version information "
               "and exit\n");
    gossip_err("  -p, --pidfile <file>\twrite process id to file\n");
}

static int server_parse_cmd_line_args(int argc, char **argv)
{
    int ret = 0, option_index = 0;
    const char *cur_option = NULL;
    static struct option long_opts[] =
    {
        {"foreground",0,0,0},
        {"mkfs",0,0,0},
        {"help",0,0,0},
        {"rmfs",0,0,0},
        {"version",0,0,0},
        {"pidfile",1,0,0},
        {0,0,0,0}
    };

    while ((ret = getopt_long(argc, argv,"dfhrvp:",
                              long_opts, &option_index)) != -1)
    {
        switch (ret)
        {
            case 0:
                cur_option = long_opts[option_index].name;
                assert(cur_option);

                if (strcmp("foreground", cur_option) == 0)
                {
                    goto do_foreground;
                }
                else if (strcmp("mkfs", cur_option) == 0)
                {
                    goto do_mkfs;
                }
                else if (strcmp("help", cur_option) == 0)
                {
                    goto do_help;
                }
                else if (strcmp("rmfs", cur_option) == 0)
                {
                    goto do_rmfs;
                }
                else if (strcmp("version", cur_option) == 0)
                {
                    goto do_version;
                }
                else if (strcmp("pidfile", cur_option) == 0)
                {
                    goto do_pidfile;
                }
                break;
            case 'v':
          do_version:
                printf("%s (mode: %s)\n", PVFS2_VERSION,
                       SERVER_STORAGE_MODE);
                return PVFS2_VERSION_REQUEST;
            case 'r':
          do_rmfs:
                s_server_options.server_remove_storage_space = 1;
            case 'f':
          do_mkfs:
                s_server_options.server_create_storage_space = 1;
                break;
            case 'd':
          do_foreground:
                s_server_options.server_background = 0;
                break;
            case 'p':
          do_pidfile:
                s_server_options.pidfile = optarg;
                if(optarg[0] != '/')
                {
                    gossip_err("Error: pidfile must be specified with an absolute path.\n");
                    goto parse_cmd_line_args_failure;
                }
                break;
            case '?':
            case 'h':
          do_help:
            default:
          parse_cmd_line_args_failure:
                usage(argc, argv);
                return 1;
        }
    }

    if (argc == 1)
    {
        goto parse_cmd_line_args_failure;
    }
    return 0;
}

/* server_post_unexpected_recv()
 *
 * Allocates space for an unexpected BMI message and posts this.
 *
 * Returns 0 on success, -PVFS_error on failure.
 */
static int server_post_unexpected_recv(job_status_s *js_p)
{
    int ret = -PVFS_EINVAL;
    job_id_t j_id;
    PINT_server_op *s_op = NULL;

    if (js_p)
    {
        s_op = (PINT_server_op *) malloc(sizeof(PINT_server_op));
        if (s_op == NULL)
        {
            return -PVFS_ENOMEM;
        }
        memset(s_op, 0, sizeof(PINT_server_op));
        s_op->op = BMI_UNEXPECTED_OP;
        /* Add an unexpected s_ops to the list */
        qlist_add_tail(&s_op->next, &posted_sop_list);

        /*
          TODO: Consider the optimization of enabling immediate
          completion in this part of the code (see the mailing list
          thread from Feb. 2003 on pvfs2-internal).

          note: unexp_bmi_buff is really a struct that describes an
          unexpected message (it is an output parameter).
        */
        ret = job_bmi_unexp(&s_op->unexp_bmi_buff, s_op, 0,
                            js_p, &j_id, JOB_NO_IMMED_COMPLETE,
                            server_job_context);
        if (ret < 0)
        {
            PVFS_perror_gossip("Error: job_bmi_unexp failure", ret);
            free(s_op);
            s_op = NULL;
        }
    }
    return ret;
}

/* server_purge_unexpected_recv_machines()
 *
 * removes any s_ops that were posted to field unexpected BMI messages
 *
 * returns 0 on success and -PVFS_errno on failure.
 */
static int server_purge_unexpected_recv_machines(void)
{
    struct qlist_head *tmp = NULL, *tmp2 = NULL;

    if (qlist_empty(&posted_sop_list))
    {
        gossip_err("WARNING: Found empty posted operation list!\n");
        return -PVFS_EINVAL;
    }
    qlist_for_each_safe (tmp, tmp2, &posted_sop_list)
    {
        PINT_server_op *s_op = qlist_entry(tmp, PINT_server_op, next);

        /* Remove s_op from the posted_sop_list */
        qlist_del(&s_op->next);

        /* free the operation structure itself */
        free(s_op);
    }
    return 0;
}

/* server_state_machine_start()
 *
 * initializes fields in the s_op structure and begins execution of
 * the appropriate state machine
 *
 * returns 0 on success, -PVFS_errno on failure
 */
static int server_state_machine_start(
    PINT_server_op *s_op,
    job_status_s *js_p)
{
    int ret = -PVFS_EINVAL;
    PVFS_id_gen_t tmp_id;

    ret = PINT_decode(s_op->unexp_bmi_buff.buffer,
                      PINT_DECODE_REQ,
                      &s_op->decoded,
                      s_op->unexp_bmi_buff.addr,
                      s_op->unexp_bmi_buff.size);

    s_op->req  = (struct PVFS_server_req *)s_op->decoded.buffer;
    if (ret == -PVFS_EPROTONOSUPPORT)
    {
        /* we have a protocol mismatch of some sort; try to trigger a
         * response that gives a helpful error on client side even
         * though we can't interpret what the client was asking for
         */
        s_op->op = PVFS_SERV_PROTO_ERROR;
    }
    else if (ret == 0)
    {
        s_op->op = s_op->req->op;
    }
    else
    {
        PVFS_perror_gossip("Error: PINT_decode failure", ret);
        return ret;
    }
    /* Remove s_op from posted_sop_list and move it to the inprogress_sop_list */
    qlist_del(&s_op->next);
    qlist_add_tail(&s_op->next, &inprogress_sop_list);

    /* set timestamp on the beginning of this state machine */
    id_gen_fast_register(&tmp_id, s_op);
    PINT_event_timestamp(PVFS_EVENT_API_SM, (int32_t)s_op->req->op,
                         0, tmp_id, PVFS_EVENT_FLAG_START);

    s_op->addr = s_op->unexp_bmi_buff.addr;
    s_op->tag  = s_op->unexp_bmi_buff.tag;
    s_op->current_state = PINT_state_machine_locate(s_op);

    if (!s_op->current_state)
    {
        gossip_err("Error: server does not implement request type: %d\n",
                   (int)s_op->req->op);
        PINT_decode_release(&(s_op->decoded),PINT_DECODE_REQ);
        return -PVFS_ENOSYS;
    }

    s_op->resp.op = s_op->op;
    return PINT_state_machine_invoke(s_op,js_p);
}

/* server_state_machine_alloc_noreq()
 * 
 * allocates and initializes a server state machine that can later be
 * started with server_state_machine_start_noreq()
 *
 * returns 0 on success, -PVFS_error on failure
 */
int server_state_machine_alloc_noreq(
    enum PVFS_server_op op,
    PINT_server_op **new_op)
{
    int ret = -PVFS_EINVAL;

    if (new_op)
    {
        *new_op = (PINT_server_op*)malloc(sizeof(PINT_server_op));
        if (!(*new_op))
        {
            return -PVFS_ENOMEM;
        }
        memset(*new_op, 0, sizeof(PINT_server_op));
        (*new_op)->op = op;

        /* NOTE: We do not add these state machines to the in-progress or posted sop lists */

        /* find the state machine for this op type */
        (*new_op)->current_state = PINT_state_machine_locate(*new_op);

        if (!((*new_op)->current_state))
        {
            gossip_lerr("Error: failed to start state machine "
                        "of op type %x\n", op);
            free(*new_op);
            return -PVFS_ENOSYS;
        }
        ret = 0;
    }
    return ret;
}

/* server_state_machine_start_noreq()
 * 
 * similar in purpose to server_state_machine_start(), except that it
 * kicks off a state machine instance without first receiving a client
 * side request
 *
 * PINT_server_op structure must have been previously allocated using
 * server_state_machine_alloc_noreq().
 *
 * returns 0 on success, -PVFS_error on failure
 */
int server_state_machine_start_noreq(PINT_server_op *new_op)
{
    int ret = -PVFS_EINVAL;
    job_status_s tmp_status;

    tmp_status.error_code = 0;

    if (new_op)
    {
        /* execute first state */
        ret = PINT_state_machine_invoke(new_op, &tmp_status);
        if (ret < 0)
        {
            gossip_lerr("Error: failed to start state machine.\n");
            return ret;
        }

        /* continue as long as states are immediately completing */
        while(ret == 1)
        {
            ret = PINT_state_machine_next(new_op, &tmp_status);
        }

        if (ret < 0)
        {
            gossip_lerr("Error: unhandled state machine processing "
                        "error (most likely an unhandled job error).\n");
        }
    }
    return ret;
}

/* server_state_machine_complete()
 *
 * function to be called at the completion of state machine execution;
 * it frees up any resources associated with the state machine that were
 * allocated before the state machine started executing.  Also returns
 * appropriate return value to make the state machine stop transitioning
 *
 * returns 0
 */
int server_state_machine_complete(PINT_server_op *s_op)
{
    PVFS_id_gen_t tmp_id;
    
    /* set a timestamp on the completion of the state machine */
    id_gen_fast_register(&tmp_id, s_op);
    PINT_event_timestamp(PVFS_EVENT_API_SM, (int32_t)s_op->req->op,
                         0, tmp_id, PVFS_EVENT_FLAG_END);

    /* release the decoding of the unexpected request */
    if (ENCODING_IS_VALID(s_op->decoded.enc_type))
    {
        PINT_decode_release(&(s_op->decoded),PINT_DECODE_REQ);
    }

    /* free the buffer that the unexpected request came in on */
    if (s_op->unexp_bmi_buff.buffer)
    {
        free(s_op->unexp_bmi_buff.buffer);
    }

    /* Remove s_op from the inprogress_sop_list */
    qlist_del(&s_op->next);

    /* free the operation structure itself */
    free(s_op);

    return 0;
}

struct server_configuration_s *get_server_config_struct(void)
{
    return &server_config;
}

/* parse_port_from_host_id()
 *
 * attempts to parse the port number from a BMI address.  Returns port number
 * on success, -1 on failure
 */
static int parse_port_from_host_id(char* host_id)
{
    int ret = -1;
    int port_num;
    char* port_index;
    char* colon_index;

    /* see if we have a <proto>://<hostname>:<port> format */
    port_index = rindex(host_id, ':');
    colon_index = index(host_id, ':');
    /* if so, parse the port number */
    if(port_index && (port_index != colon_index))
    {
        port_index++;
        ret = sscanf(port_index, "%d", &port_num);
    }

    /* report error if we don't find a valid port number in the string */
    if(ret != 1)
    {
        return(-1);
    }
    
    return(port_num);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
