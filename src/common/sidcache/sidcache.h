/*
 * (C) 2012 Clemson University
 *
 * See COPYING in top-level directory.
*/

#ifndef SIDCACHE_H
#define SIDCACHE_H 1

#include <db.h>
#include <string.h>
#include <stdlib.h>
#include "pvfs2-types.h"
#include "pvfs3-handle.h"
#include "sidcacheval.h"
#include "policyeval.h"
#include "sid.h"

/* these are defines just to temporarily allow compile */
//typedef int BMI_addr;
#define PVFS_LAYOUT_ROUND_ROBIN 0
#define PVFS_LAYOUT_RANDOM 1
/* remove when integrating into main orange code */

/* main SID cache database */
extern DB     *SID_db;

/* Global variable for the database environement */
extern DB_ENV *SID_envp;

/* main SID transaction */
extern DB_TXN *SID_txn;

#if 0
/* attribute secondary DBs */
extern DB     *SID_attr_index[SID_NUM_ATTR]; 

/* cursor for each secondary DB */
extern DBC    *SID_attr_cursor[SID_NUM_ATTR];
#endif

/* <===================== GLOBAL DATABASE DEFINES =====================> */
/* Used to set the in cache memory size for DB environment*/
#define CACHE_SIZE_GB (0)
#define CACHE_SIZE_MB (500)

/* Constant used to store tmp strings in a buffer */
#define TMP_BUFF_SIZE (100)

/* Size of one kilobyte */
#define KILOBYTE (1024)

/* Size of one megabyte */
#define MEGABYTE (KILOBYTE * KILOBYTE)

/* Minimum size of bulk retrieve in bytes. Must be page size. */
#define BULK_MIN_SIZE (KILOBYTE * 8)

/* Size of string representation of SID */
#define SID_STR_LEN (37)

/* <==================== INITIALIZATION FUNCTIONS =====================> */
/*
 * This function initializes a SID_cacheval_t struct to default values
 */
extern void SID_cacheval_init(SID_cacheval_t **cacheval_t);

/*
 * This function creates a SID_cacheval_t struct with the attributes that are passed to
 * this function by dynamically creating the SID_cacheval_t. The url attribute cannot
 * be null otherwise the SID_cacheval_t is not dynamically created
 *
 * Returns 0 on success, otherwise -1 is returned
 */
extern int SID_cacheval_alloc(SID_cacheval_t **cacheval_t,
                              int sid_attributes[], 
                              BMI_addr sid_bmi,
                              char *sid_url);

/*
 * This function clean up a SID_cacheval_t struct by freeing the dynamically
 * created SID_cacheval_t struct
*/
extern void SID_cacheval_free(SID_cacheval_t **cacheval_t);

/*
 * This function packs up the data for the SID_cacheval_t to store in the
 * sid cache
 */
extern void SID_cacheval_pack(const SID_cacheval_t *the_sids_attrs, DBT *data);

/*
 * This function unpacks the data recieved from the database, mallocs the 
 * SID_cacheval_t struct, and sets the values inside of the SID_cacheval_t 
 * struct with the data retrieved from the database
 */
extern void SID_cacheval_unpack(SID_cacheval_t **the_sids_attrs, DBT *data);

/* <======================= SID CACHE FUNCTIONS =======================> */
/*
 * This function loads the contents of the sid cache from an input file. The
 * number of sids in the file is returned though the parameter db_records
 * 
 * Returns 0 on success, otherwise returns an error code
 */
extern int SID_cache_load(DB **dbp, FILE *inpfile, int *num_db_records);

/*
 * This function dumps the contents of the sid cache in ASCII to the file
 * specified through the outpfile parameter
 *
 * Returns 0 on success, otherwise returns an error code
 */
extern int SID_cache_store(DBC *cursorp,
                           FILE *outpfile,
                           int db_records,
                           SID_server_list_t *sid_list);

/*
 * This function stores the sid into the sid cache
 *
 * Returns 0 on success, otherwise returns an error code
 */
extern int SID_cache_add_server(DB **dbp,
                                const PVFS_SID *sid_server,
                                const SID_cacheval_t *cacheval_t, 
                                int *db_records);

/*
 * This function searches for a sid in the sid cache. The sid  value
 * (sid_server parameter) must be initialized before this function is
 * used. The SID_cacheval_t will malloced and set to the values of the attibutes
 * in the database for the sid if it is found in the database
 *
 * Returns 0 on success, otherwise returns an error code
 */
extern int SID_cache_lookup_server(DB **dbp,
                                   const PVFS_SID *sid_server,
                                   SID_cacheval_t **cacheval_t);

/*
 * This function searches for a sid in the sid cache, retrieves the struct,
 * malloc's the char * passed in, and copies the bmi address of the retrieved
 * struct into that char *.
 */
extern int SID_cache_lookup_bmi(DB **dbp,
                                const PVFS_SID *search_sid,
                                char **bmi_addr);

/*
 * This function updates the sid in the sid cache to all the new values 
 * (attributes, bmi address, and url) that are in the SID_cacheval_t parameter
 * to this function if a sid with a matching as the sid_server parameter
 * is found in the sid cache
 *
 * Returns 0 on success, otherwise returns an error 
 */
extern int SID_cache_update_server(DB **dbp,
                                   const PVFS_SID *sid_server,
                                   SID_cacheval_t *new_attrs);

/*
 * This function updates the attributes for a sid in the database if a sid
 * with a matching sid_server parameter is found in the database
 *
 * Returns 0 on success, otherwise returns an error code
 */
extern int SID_cache_update_attrs(DB **dbp,
                                  const PVFS_SID *sid_server,
                                  int new_attr[]);

extern int SID_cache_copy_attrs(SID_cacheval_t *current_sid_attrs, 
                                int new_attr[]);

/*
 * This function updates the bmi address for a sid in the database if a sid
 * with a matching sid_server parameter is found in the database
 *
 * Returns 0 on success, otherwise returns an error code
 */
extern int SID_cache_update_bmi(DB **dbp,
                                const PVFS_SID *sid_server,
                                BMI_addr new_bmi_addr);

extern int SID_cache_copy_bmi(SID_cacheval_t *current_sid_attrs, 
                              BMI_addr new_bmi_addr);

/*
 * This function updates the url address for a sid in the database if a sid
 * with a matching sid_server parameter is found in the database
 *
 * Returns 0 on success, otherwise returns an error code
 */
extern int SID_cache_update_url(DB **dbp,
                                const PVFS_SID *sid_server,
                                char *new_url);

extern int SID_cache_copy_url(SID_cacheval_t **current_sid_attrs, 
                              char *new_url);

/*
 * This function deletes a record from the sid cache if a sid with a matching
 * sid_server parameter is found in the database
 *
 * Returns 0 on success, otherwise returns an error code 
 */
extern int SID_cache_delete_server(DB **dbp,
                                   const PVFS_SID *sid_server,
                                   int *db_records);

/*
 * This function retrieves entries from a primary database and stores them into a
 * bulk buffer DBT. 
 *
 * The minimum amount that can be retrieved is 8 KB of entries.
 * 
 * You must specify a size larger than 8 KB by putting an amount into the two size
 * parameters.
 *
 * The output DBT is malloc'ed so take care to make sure it is freed, either by
 * using it in a bulk_insert or by manually freeing.
 *
 * If the cache is larger than the buffer, the entry that does not fit is saved in
 * bulk_next_key global variable.
 */
extern int SID_bulk_retrieve_from_sid_cache(int size_of_retrieve_kb,
                                            int size_of_retrieve_mb, 
                                            DB *dbp,
                                            DBC **dbcursorp,
                                            DBT *output);

/*
 * This function inserts entries from the input bulk buffer DBT
 * into a database.
 *
 * The function uses the output from SID_bulk_retrieve as its input.
 *
 * The malloc'ed bulk buffer DBT's are freed at the end of this function.
 */
extern int SID_bulk_insert_into_sid_cache(DB *dbp, DBT *input);


/* <======================== DATABASE FUNCTIONS =======================> */
/* 
 * The functions zeros out the database types key and data values so they
 * are initialized before they are used
 */
extern void SID_zero_dbt(DBT *key, DBT *data, DBT *pkey);


/*
***********************************************************************
* The following is the order in which the functions should be called to
* the open the sidcache:                                              
* 1. SID_create_open_environment (
*                    If an environment is not needed then this function 
*                    can be skipped and the environment variable can be
*                    passed as NULL to rest of the database functions) 
* 2. SID_create_open_sid_cache
* 3. SID_create_open_assoc_sec_dbs
* 4. SID_create_open_dbcs        
***********************************************************************
*/
/*
 * This function creates and opens the environement handle
 *
 * Returns 0 on success, otherwise returns an error code
 */
extern int SID_create_open_environment(DB_ENV **envp);

/*
 * This function creates and opens the primary database handle, which
 * has the DB_HASH as the access method
 *
 * Returns 0 on success, otherwise returns an error code 
 */
extern int SID_create_open_sid_cache(DB_ENV *envp, DB **dbp);

/*
 * This function creates, opens, and associates the secondary attribute
 * database handles and sets the database pointers in the secondary_dbs
 * array to point at the correct database. The access method for the 
 * secondary databases is DB_TREE and allows for duplicate records with
 * the same key values
 *
 * Returns 0 on success, otherwise returns an error code
 */
extern int SID_create_open_assoc_sec_dbs(DB_ENV *envp,
                                         DB *dbp,
                                         DB *secondary_dbs[], 
                                         int (* secdbs_callback_functions[])(
                                                     DB *pri,
                                                     const DBT *pkey,
                                                     const DBT *pdata,
                                                     DBT *skey));

/*
 * This function creates and opens the database cursors set to the secondary
 * attribute databases in the database cursor pointer array
 *
 * Returns 0 on success, otherwise returns an error code
 */
extern int SID_create_open_dbcs(DB *secondary_dbs[], DBC *db_cursors[]);


/************************************************************************
* The following is the order in which the functions should be called to *
* the close the sidcache:                                               *
* 1. SID_close_dbcs                                                     *
* 2. SID_close_dbs_env                                                  *
************************************************************************/
/*
 * This function closes the database cursors in the cursors pointer array
 * 
 * Returns 0 on success, otherwise returns an error code
 */
extern int SID_close_dbcs(DBC *db_cursors[]);

/*
 * This function closes the primary database, secondary attribute databases,
 * and environment handles
 *
 * Returns 0 on success, otherwise returns an error code
 */
extern int SID_close_dbs_env(DB_ENV *envp, DB *dbp, DB *secondary_dbs[]);

#endif /* SIDCACHE_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
