/*
 * (C) 2012 Clemson University
 *
 * See COPYING in top-level directory.
*/

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <pvfs3-handle.h>
#include <pvfs2-debug.h>
#include <gossip.h>
#include <server-config-mgr.h>
#include "sidcache.h"

/* Length of string representation of PVFS_SID */
/* #define SID_STR_LEN (37) in sidcache.h */

/* Used by bulk retrieve function to keep track of the sid obtained
 * from the database during bulk retrieval 
 */
DBT bulk_next_key;

/* Global attribute positions in the input file used for making
 * and array of the positions 
 */
static int *attr_positions;

/* Global number of attributes in the input file and if any of the
 * attributes are invalid the valid_attrs_in_file is updated to 
 * reflect only the valid attributes 
 */
static int attrs_in_file;
static int valid_attrs_in_file;


/* Global database variables */
DB *SID_db;                         /* Primary database (sid cache) */
DB_ENV *SID_envp;                   /* Env for sid cache and secondary dbs */
DB *SID_attr_index[SID_NUM_ATTR];   /* Array of secondary databases */
DBC *SID_attr_cursor[SID_NUM_ATTR]; /* Array of secondary database cursors */
DB_TXN *SID_txn;                    /* Main transaction variable */
                                    /* (transactions are currently not used */
                                    /*  with the sid cache) */


/* <========================= STATIC FUNCTIONS ============================> */
static int SID_initialize_secondary_dbs(DB *secondary_dbs[]);
static int SID_initialize_database_cursors(DBC *db_cursors[]);
static int SID_parse_input_file_header(FILE *inpfile, int *records_in_file);

/* <====================== INITIALIZATION FUNCTIONS =======================> */

/* 
 * This function initializes the indices of a secondary DB to all be equal
 * to NULL
 *
 * On success 0 is returned, otherwise the index that is not equal to NULL
 * is returned unless the index is 0, then -1 is returned
 */
int SID_initialize_secondary_dbs(DB *secondary_dbs[])
{
    int ret =  0;
    int i = 0;

    memset(secondary_dbs, '\0', (sizeof(DB *) * SID_NUM_ATTR));

    for(i = 0; i < SID_NUM_ATTR; i++)
    {
        if(secondary_dbs[i] != NULL)
        {
            if(i == 0)
            {
                return(-1);
            }
            else
            {
                return(i);
            }
        }
    }

    return(ret);
}

/*
 * This function initializes the indices of a DBC array to all be equal to NULL
 *
 * On success 0 is returned, otherwise the index that is not equal to NULL
 * is returned unless the index is 0, then -1 is returned
 */
int SID_initialize_database_cursors(DBC *db_cursors[])
{
    int ret = 0;
    int i = 0;

    memset(SID_attr_cursor, '\0', (sizeof(DBC *) * SID_NUM_ATTR));

    for(i = 0; i < SID_NUM_ATTR; i++)
    {
        if(SID_attr_cursor[i] != NULL)
        {
            if(i == 0)
            {
                return(-1);
            }
            else
            {
                return(i);
            }
        }
    }

    return(ret);
}

/*
 * This function initializes a SID_cacheval_t struct to default values
*/
void SID_initialize_SID_cacheval_t(SID_cacheval_t **cacheval_t)
{
    memset((*cacheval_t)->attr, -1, (sizeof(int) * SID_NUM_ATTR));
    memset(&((*cacheval_t)->bmi_addr), -1, sizeof(BMI_addr));
}

/* <====================== SID CACHE FUNCTIONS ============================> */

/** HELPER for SID_LOAD
 *
 * This function parses the first two lines in the input file and gets
 * the number of attributes per sid, number of sids, and the string representations
 * of the int attributes for the sids. It gets the attributes that each sid has
 * parsing the strings representations of the attributes.
 * This function also sets the attr_positions array to make sure that the
 * attributes in the input file go into their correct positions in the
 * SID_cacheval_t attrs array
 *
 * Returns 0 on success, otherwise returns an error code
 * The attr_position array is malloced inside of this file and is freed
 * inside the close_dbs_env function
 */
int SID_parse_input_file_header(FILE *inpfile, int *records_in_file)
{
    int i = 0, j = 0;             /* Loop index variables */
    char **attrs_strings;         /* Strings of the attributes in the file */
    char tmp_buff[TMP_BUFF_SIZE]; /* Temporary string buffer to read the
                                     attribute strings from the input file */

    /* Checking to make sure the input file is open, the function
     * load_sid_cache_from_file should have opened the file
     */
    if(!inpfile)
    {
        gossip_err("File is not opened. Exiting load_sid_cache_from_file\n");
        return(-1);
    }
    
    /* Getting the total number of attributes from the file */
    fscanf(inpfile, "%s", tmp_buff);
    fscanf(inpfile, "%d", &attrs_in_file);
    if(attrs_in_file > SID_NUM_ATTR || attrs_in_file < 1)
    {
        gossip_err("The number of attributes in the input file "
                   "was not within the proper range\n");
        gossip_err("The contents of the database will not be read "
                   "from the inputfile\n");
        valid_attrs_in_file = 0;
        return(-1);
    }
    
    /* Setting the total number of valid attributes in the file to the current
     * number of attributes in the file
     */
    valid_attrs_in_file = attrs_in_file;

    /* Getting the number of sids in from the file */
    fscanf(inpfile, "%s", tmp_buff);
    fscanf(inpfile, "%d", records_in_file);

    /* Checking to make sure the input file has the number of sids as the
     *  entry in the input file
     */
    if(*records_in_file == 0)
    {
        gossip_err("There are no sids in the input file\n");
        return(-1);
    }
    
    /* Mallocing space to hold the name of the attributes in the file 
     * and initializing the attributes string array 
     */
    attrs_strings = (char **)malloc(sizeof(char *) * attrs_in_file);
    memset(attrs_strings, '\0', sizeof(char *) * attrs_in_file);

    /* Mallocing space to hold the positions of the attributes in the file for
     * the cacheval_t attribute arrays and initializing the position array 
     */
    attr_positions = (int *)malloc(sizeof(int) * attrs_in_file);
    memset(attr_positions, 0, (sizeof(int) * attrs_in_file));
    
    /* Getting the attribute strings from the input file */
    for(i = 0; i < attrs_in_file; i++)
    {
        fscanf(inpfile, "%s", tmp_buff);
        attrs_strings[i] = (char *)malloc(sizeof(char) *
                                          (strlen(tmp_buff) + 1));
        strncpy(attrs_strings[i], tmp_buff, (strlen(tmp_buff) + 1));
    }

    /* Getting the correct positions from the input file for the
     * attributes in the SID_cacheval_t attrs array 
     */
    for(i = 0; i < attrs_in_file; i++)
    {
        for(j = 0; j < SID_NUM_ATTR; j++)
        {
            if(!strcmp(attrs_strings[i], SID_attr_map[j]))
            {
                attr_positions[i] = j;
                break;
            }
        }
        /* If the attribute string was not a valid option it will be ignored
         * and not added to the sid cache 
         */
        if(j == SID_NUM_ATTR)
        {
            gossip_debug(GOSSIP_SIDCACHE_DEBUG, 
                         "Attribute: %s is an invalid attribute, "
                         "and it will not be added\n", attrs_strings[i]);
            attr_positions[i] = -1;
            valid_attrs_in_file--;
        }
    }

    /* Freeing the dynamically allocated memory */
    for(i = 0; i < attrs_in_file; i++)
    {
        free(attrs_strings[i]);
    }
    free(attrs_strings);

    return(0);
}



/** SID_LOAD
 *
 * This function loads the contents of the sid cache from a input file. The
 * number of sids in the file is returned through the parameter db_records
 *
 * Returns 0 on success, otherwise returns an error code
 */
int SID_load_sid_cache_from_file(DB **dbp,
                                 const char *file_name,
                                 int *num_db_records)
{
    int ret = 0;                      /* Function return value */
    int i = 0, j = 0;                 /* Loop index variables */
    FILE *inpfile = NULL;             /* file ptr for input file */
    int attr_pos_index = 0;           /* Index of the attribute from the */
                                      /*   file in the SID_cacheval_t attrs */
    int records_in_file = 0;          /* Total number of sids in input file */
    int sid_attributes[SID_NUM_ATTR]; /* Temporary attribute array to hold */
                                      /*   the attributes from the input file */
    int throw_away_attr = 0;          /* If the attribute is not a valid */
                                      /*   choice its value will be ignored */
    BMI_addr tmp_bmi = 0;             /* Temporary BMI_addr for the bmi */
                                      /*   addresses from input file */
    char tmp_url[TMP_BUFF_SIZE];      /* Temporary string for the url's from */
                                      /*   the input file */
    char tmp_sid_str[SID_STR_LEN];    /* Temporary string to get the PVFS_SID */
                                      /*   string representation from input */
                                      /*   file */
    PVFS_SID current_sid;             /* Temporary location to create the */
                                      /*   PVFS_SID from the tmp_SID_str */
    SID_cacheval_t *current_sid_cacheval; /* Sid's attributes from the input file */

    /* Opening input file */
    inpfile = fopen(file_name, "r");
    if(!inpfile)
    {
        gossip_err("Could not open the file %s in "
                   "SID_load_cache_from_file\n", file_name);
        return(-1);
    }

    /* Getting the attributes from the input file */
    ret = SID_parse_input_file_header(inpfile, &records_in_file);
    if(ret)
    {
        fclose(inpfile);
        return(ret);
    }

    for(i = 0; i < records_in_file; i++)
    {
        /* Initializing the temporary attribute array */
        /* so all index's are currently -1 */
        memset(sid_attributes, -1, (sizeof(int) * SID_NUM_ATTR));

        /* Getting the attributes from the input file and placing them in the
         * correct position in the attrs array in the SID_cacheval_t struct 
         */
        for(j = 0; j < attrs_in_file; j++)
        {
            if(attr_positions[attr_pos_index] != -1)
            {
                /*fscanf(inpfile, "%d",
                         &(current_sid_cacheval.sid_attrs->attrs[
                                         attr_positions[attr_pos_index]]));*/
                fscanf(inpfile, "%d",
                      &(sid_attributes[attr_positions[attr_pos_index]]));
            }
            else
            {
                fscanf(inpfile, "%d", &throw_away_attr);
            }
            attr_pos_index++;
        }
        /* Resetting the attribute position index to 0 so the
         * next SID_cacheval_t will have its attributes in the
         * correct positions in the attrs array 
         */
        attr_pos_index = 0;

        /* Getting the bmi address */
        fscanf(inpfile, "%d", &tmp_bmi);

        /* Getting the url */
        fscanf(inpfile, "%s", tmp_url);

        /* Getting the sid's string representation from the input file */
        fscanf(inpfile, "%s", tmp_sid_str);

        /* convert to binary */
        ret = PVFS_SID_str2bin(tmp_sid_str, &current_sid);
        if(ret)
        {
            /* Skips adding sid to sid cache */
            goto bad_sid;
        }

        ret = SID_create_SID_cacheval_t(&current_sid_cacheval,
                                        sid_attributes,
                                        tmp_bmi, tmp_url);
        if(ret)
        {
            return(ret);
        }
    
        /* Storing the current sid from the input file into the sid cache */
        ret = SID_store_sid_into_sid_cache(dbp,
                                           &current_sid,
                                           current_sid_cacheval,
                                           num_db_records);
        if(ret)
        {
            fclose(inpfile);
            SID_clean_up_SID_cacheval_t(&current_sid_cacheval);
            return(ret);
        }

        SID_clean_up_SID_cacheval_t(&current_sid_cacheval);

        bad_sid:
            gossip_debug(GOSSIP_SIDCACHE_DEBUG, "Error parsing PVFS_SID in "
                         "SID_load_cache_from_file function\n");
    }
   
    fclose(inpfile);
    return(ret);
}

/** SID_ADD
 * 
 * This function stores a sid into the sid_cache
 *
 * Returns 0 on success, otherwise returns error code
*/
int SID_store_sid_into_sid_cache(DB **dbp,
                                 PVFS_SID *sid_server,
                                 SID_cacheval_t *cacheval_t,
                                 int *num_db_records)
{
    int ret = 0;          /* Function return value */
    DBT key, data;        /* BerekeleyDB k/v pair sid value(SID_cacheval_t) */

    if(PVFS_SID_is_null(sid_server))
    {
        gossip_debug(GOSSIP_SIDCACHE_DEBUG, "Error: PVFS_SID in "
                     "SID_store_sid_into "
                     "sid_cache function is currently NULL\n");
        return(-1);
    }

    SID_zero_dbt(&key, &data, NULL);

    key.data = sid_server;
    key.size = sizeof(PVFS_SID);

    /* Marshalling the data of the SID_cacheval_t struct */
    /* to store it in the primary db */
    SID_pack_SID_cacheval_data(cacheval_t, &data);

    /* Placing the data into the database */
    ret = (*dbp)->put(*dbp,            /* Primary database pointer */
                      NULL,            /* Transaction pointer */
                      &key,            /* PVFS_SID */
                      &data,           /* Data is marshalled SID_cacheval_t */
                      DB_NOOVERWRITE); /* Do not allow for overwrites because */
                                       /* this is the primary database */

    if(ret)
    {
        gossip_debug(GOSSIP_SIDCACHE_DEBUG, "Error inserting sid into the \
                     sid cache : %s\n", db_strerror(ret));
        return(ret);
    }

    /* Updating the record count if the sid was successfully added
     * to the sid cache 
     */
    if(num_db_records != NULL)
    {
        *num_db_records += 1;
    }

    return(ret);
}

/** SID_LOOKUP
 *
 * This function searches for a SID in the sidcache. The PVFS_SID value
 * (sid_server parameter) must be initialized before this function is used.
 * The SID_cacheval_t will malloced and set to the values of the attibutes
 * in the database for the sid if it is found in the database
 *
 * Returns 0 on success, otherwise returns an error code
 */
int SID_retrieve_sid_from_sid_cache(DB **dbp,
                                    PVFS_SID *sid_server,
                                    SID_cacheval_t **cacheval_t)
{
    int ret = 0;
    DBT key, data;

    SID_zero_dbt(&key, &data, NULL);

    key.data = sid_server;
    key.size = sizeof(PVFS_SID);
   
    ret = (*dbp)->get(*dbp,  /* Primary database pointer */
                      NULL,  /* Transaction Handle */
                      &key,  /* Sid_sid_t sid */
                      &data, /* Data is marshalled SID_cacheval_t struct */
                      0);    /* get flags */
    if(ret)
    {
        gossip_debug(GOSSIP_SIDCACHE_DEBUG, "Error getting sid from "
                     "sid cache : %s\n", db_strerror(ret));
        return(ret);
    }

    /* Unmarshalling the data from the database for the SID_cacheval_t struct */
    SID_unpack_SID_cacheval_data(cacheval_t, &data);
    
    /* *cacheval_t = malloc(data.size); */
    /* memcpy(*cacheval_t, data.data, data.size); */

    return(ret);
}

/** HELPER - SID_LOOKUP and assign bmi_addr
 *
 * This function searches for a sid in the sid cache, retrieves the struct,
 * malloc's the char * passed in, and copies the bmi address of the retrieved
 * struct into that char *.
 */
int SID_bmi_lookup_from_sid_cache(DB **dbp,
                                  PVFS_SID *search_sid,
                                  char **bmi_addr)
{
    SID_cacheval_t *temp;
    int ret;

    /* Retrieve the corresponding value */
    ret = SID_retrieve_sid_from_sid_cache(dbp, search_sid, &temp);

    if (ret)
    {
        gossip_debug(GOSSIP_SIDCACHE_DEBUG,
                     "Error retrieving from sid cache\n");
        return ret;
    }

    /* Malloc the outgoing BMI address char * to be size of retrieved one */
    *bmi_addr = malloc(strlen(temp->url) + 1);

    /* Copy retrieved BMI address to outgoing one */
    strcpy(*bmi_addr, temp->url);

    /* Free any malloc'ed memory other than the outgoing BMI address */
    free(temp);

    return ret;
}

/** HELPER - assigns DBT for cacheval
 *
 * This function marshalls the data for the SID_cacheval_t to store in the 
 * sidcache
*/
void SID_pack_SID_cacheval_data(SID_cacheval_t *the_sids_attrs,
                                DBT *data)
{
    data->data = the_sids_attrs;
    data->size = (sizeof(int) * SID_NUM_ATTR) +
                 sizeof(BMI_addr) +
                 strlen(the_sids_attrs->url) +
                 1;
}

/** HELPER - copies cacheval out of DBT
 *
 * This function unpacks the data recieved from the database, mallocs the 
 * SID_cacheval_t struct, and sets the values inside of the SID_cacheval_t 
 * struct with the data retrieved from the database
 */
void SID_unpack_SID_cacheval_data(SID_cacheval_t **the_sids_attrs, DBT *data)
{              
    *the_sids_attrs = malloc(data->size);
    memcpy(*the_sids_attrs, data->data, data->size);
}

/** SID_UPDATE
 *
 * This function updates a record in the sid cache to any new values 
 * (attributes, bmi address, or url) that are in the SID_cacheval_t parameter
 * to this function if a record with a SID matching the sid_server parameter
 * is found in the sidcache
 *
 * Returns 0 on success, otherwise returns error code
 */
int SID_update_sid_in_sid_cache(DB **dbp,
                                PVFS_SID *sid_server,
                                SID_cacheval_t *new_attrs)
{
    int ret = 0;                   /* Function return value */
    SID_cacheval_t *current_attrs; /* Temp SID_cacheval_t used to get current 
                                      attributes of the sid from the sid */
   
    if(new_attrs == NULL)
    {
        gossip_debug(GOSSIP_SIDCACHE_DEBUG, "The new attributes passed to "
                     "SID_update_sid_in_sid_cache is NULL\n");
        return(-1);
    }
 
    /* Getting the sid from the sid cache */
    ret = SID_retrieve_sid_from_sid_cache(dbp,
                                          sid_server,
                                          &current_attrs);
    if(ret)
    {
        return(ret);
    }

    /* Updating the old attributes to the new values */
    SID_update_attributes_in_sid(NULL,
                                 NULL,
                                 current_attrs,
                                 new_attrs->attr);
        
    /* Updating the old bmi address to the new bmi_address */
    SID_update_bmi_address_in_sid(NULL,
                                  NULL,
                                  current_attrs,
                                  new_attrs->bmi_addr);
   
    /* Updating the old url to the new url */
    SID_update_url_in_sid(NULL,
                          NULL,
                          &current_attrs,
                          new_attrs->url);

    /* Deleting the old record from the sid cache */
    ret = SID_delete_sid_from_sid_cache(dbp,
                                        sid_server,
                                        NULL);
    if(ret)
    {
        return(ret);
    }
    
    /* Adding the updated SID_cacheval_t struct back to the sid cache */
    ret = SID_store_sid_into_sid_cache(dbp,
                                       sid_server,
                                       current_attrs,
                                       NULL);
    if(ret)
    {
        return(ret);
    }

    /* Freeing the dynamically created bmi address in the SID_cacheval_t
     * struct 
     */
    SID_clean_up_SID_cacheval_t(&current_attrs);

    return(ret);
}

/** I don't understand what this function is for
 *  seems like we already have an update function, and there doesn't
 *  seem to be a reason to delete an old record and insert a new one
 *  if we can update.  Also return code is sketch.
 *  Maybe this should be a helper to update attrs in memory?
 *
 * This function updates the attributes for a sid in the database if a sid
 * with a matching sid_server parameter is found in the database
 *
 * Returns 0 on success, otherwise returns an error code
 */
int SID_update_attributes_in_sid(DB **dbp,
                                 PVFS_SID *sid_server,
                                 SID_cacheval_t *current_sid_attrs,
                                 int new_attr[])
{
    int ret = 0;
    int i = 0;   /* Loop index variable */

    /* If sid_server is not passed in as NULL then only the attr's 
     * will be changed for the sid 
     */
    if(sid_server)
    {
        SID_cacheval_t *sid_attrs;
        /* Getting the sid from the sid cache */
        ret = SID_retrieve_sid_from_sid_cache(dbp,
                                              sid_server,
                                              &sid_attrs);
        if(ret)
        {
            return(ret);
        }
        
        for(i = 0; i < SID_NUM_ATTR; i++)
        {
            /* If the new value is different the original value, then the 
             * original value is updated to the new attribute value 
             */
            if(sid_attrs->attr[i] != new_attr[i])
            {
                sid_attrs->attr[i] = new_attr[i];
            }
        }

        /* Deleting the old record from the sid cache */
        ret = SID_delete_sid_from_sid_cache(dbp,
                                            sid_server,
                                            NULL);
        if(ret)
        {
            return(ret);
        }

        ret = SID_store_sid_into_sid_cache(dbp,
                                           sid_server,
                                           sid_attrs,
                                           NULL);
        if(ret)
        {
            SID_clean_up_SID_cacheval_t(&sid_attrs);
            return(ret);
        }

        SID_clean_up_SID_cacheval_t(&sid_attrs);
        return(ret);    
    }
    /* The attr's are being updated with other attributes with
     * SID_update_sid_in_sid_cache
     * function, so only the current_sid_attrs need to change 
     */
    else
    {
        for(i = 0; i < SID_NUM_ATTR; i++)
        {
            /* If the new value is different the original value, then the 
             * original value is updated to the new attribute value 
             */
            if(current_sid_attrs->attr[i] != new_attr[i])
            {
                current_sid_attrs->attr[i] = new_attr[i];
            }
        }
        return(ret);
    }
}

/*
 * This function updates the bmi address for a sid in the database if a sid
 * with a matching sid_server parameter is found in the database
 *
 * Returns 0 on success, otherwise returns an error code
 */
int SID_update_bmi_address_in_sid(DB **dbp,
                                  PVFS_SID *sid_server,
                                  SID_cacheval_t *current_sid_attrs,
                                  BMI_addr new_bmi_addr)
{
    int ret = 0;

    /* If sid_server is not passed in as NULL then only the bmi address 
     * will be changed for the sid 
     */
    if(sid_server)
    {
        SID_cacheval_t *sid_attrs;
        /* Getting the sid from the sid cache */
        ret = SID_retrieve_sid_from_sid_cache(dbp,
                                              sid_server,
                                              &sid_attrs);
        if(ret)
        {
            return(ret);
        }
        
        if(sid_attrs->bmi_addr != new_bmi_addr)
        {
            sid_attrs->bmi_addr = new_bmi_addr;

            /* Deleting the old record from the sid cache */
            ret = SID_delete_sid_from_sid_cache(dbp,
                                                sid_server,
                                                NULL);
            if(ret)
            {
            return(ret);
            }

            ret = SID_store_sid_into_sid_cache(dbp,
                                               sid_server,
                                               sid_attrs,
                                               NULL);
            if(ret)
            {
                SID_clean_up_SID_cacheval_t(&sid_attrs);
                return(ret);
            }
        }

        SID_clean_up_SID_cacheval_t(&sid_attrs);
        return(ret);    
    }
    else
    {
        /* The bmi address is being updated with other attributes with
         * SID_update_sid_in_sid_cache
         * function, so only the current_sid_attrs need to change 
         */
        if(current_sid_attrs->bmi_addr != new_bmi_addr)
        {
            current_sid_attrs->bmi_addr = new_bmi_addr;
        }
        return(ret);
    }   
}



/*
 * This function updates the url address for a sid in the database if a sid
 * with a matching sid_server parameter is found in the database
 *
 * Returns 0 on success, otherwise returns an error code
 */
int SID_update_url_in_sid(DB **dbp,
                          PVFS_SID *sid_server,
                          SID_cacheval_t **current_sid_attrs,
                          char *new_url)
{
    int ret = 0;
    int tmp_attrs[SID_NUM_ATTR];
    BMI_addr tmp_bmi;

    if(!new_url)
    {
        gossip_debug(GOSSIP_SIDCACHE_DEBUG,
                     "The url passed into SID_update_url_in_sid \
                     function is currently NULL\n");
        return(-1);
    }

    /* If sid_server is not passed in as NULL then only the url will
     * be changed for the sid 
     */
    if(sid_server)
    {

        SID_cacheval_t *sid_attrs;

        /* Getting the sid from the sid cache */
        ret = SID_retrieve_sid_from_sid_cache(dbp,
                                              sid_server,
                                              &sid_attrs);
        if(ret)
        {
            return(ret);
        }
   
        if(!(strcmp(sid_attrs->url, new_url)))
        {
            /* The new url is the same as the current */
            /* url so no changes need to happen */
            SID_clean_up_SID_cacheval_t(&sid_attrs);
            return(ret);
        }
        else
        {
            memcpy(tmp_attrs, sid_attrs->attr, sizeof(int) * SID_NUM_ATTR);
            memcpy(&tmp_bmi, &(sid_attrs->bmi_addr), sizeof(BMI_addr));
            
            SID_clean_up_SID_cacheval_t(&sid_attrs);
            ret = SID_create_SID_cacheval_t(&sid_attrs,
                                            tmp_attrs,
                                            tmp_bmi,
                                            new_url);    
            if(ret)
            {
                return(ret);
            }        

            /* Deleting the old record from the sid cache */
            ret = SID_delete_sid_from_sid_cache(dbp,
                                                sid_server,
                                                NULL);
            if(ret)
            {
                return(ret);
            }

            /* Adding the updated SID_cacheval_t struct back to the sid cache */
            ret = SID_store_sid_into_sid_cache(dbp,
                                               sid_server,
                                               sid_attrs,
                                               NULL);
            if(ret)
            {
                SID_clean_up_SID_cacheval_t(&sid_attrs);
                return(ret);
            }
    
            SID_clean_up_SID_cacheval_t(&sid_attrs);
            return(ret);    
        }
    }
    /* The url is being updated with other attributes with
     * SID_update_sid_in_sid_cache
     * function, so only the current_sid_attrs need to change 
     */
    else
    {
        /* The new new url is the same as the old url so nothing needs
           to be updated */
        if((!strcmp((*current_sid_attrs)->url, new_url)) || (!new_url))
        {
            return(ret);
        }
        else
        {
            memcpy(tmp_attrs, (*current_sid_attrs)->attr,
                              sizeof(int) * SID_NUM_ATTR);
            memcpy(&tmp_bmi, &((*current_sid_attrs)->bmi_addr),
                              sizeof(BMI_addr));
            
            /* Freeing the current SID_cacheval_t struct to change the
             * url address to the new address 
             */
            SID_clean_up_SID_cacheval_t(current_sid_attrs);
        
            /* Setting the current bmi address to the new values */
            ret = SID_create_SID_cacheval_t(current_sid_attrs,
                                            tmp_attrs,
                                            tmp_bmi,
                                            new_url);
            if(ret)
            {
                return(ret);
            }
            
            return(ret);
        }
    }
}

/** SID_REMOVE
  *
  * This function deletes a record from the sid cache if a sid with a matching
  * sid_server parameter is found in the database
  *
  * Returns 0 on success, otherwise returns an error code 
  */
int SID_delete_sid_from_sid_cache(DB **dbp,
                                  PVFS_SID *sid_server,
                                  int *db_records)
{
    int ret = 0; /* Function return value */
    DBT key;     /* Primary sid key  */

    /* Initializing the DBT key value */
    SID_zero_dbt(&key, NULL, NULL);
    
    /* Setting the values of DBT key to point to the sid to 
     * delete from the sid cache 
     */
    key.data = sid_server;
    key.size = sizeof(PVFS_SID);

    ret = (*dbp)->del(*dbp, /* Primary database (sid cache) pointer */
                      NULL, /* Transaction Handle */
                      &key, /* SID_cacheval_t key */
                      0);   /* Delete flag */
    if(ret)
    {
        gossip_debug(GOSSIP_SIDCACHE_DEBUG,
                     "Error deleting record from sid cache : %s\n",
                     db_strerror(ret));
        return(ret);
    }
  
    if(db_records != NULL)
    { 
        *db_records -= 1;
    }

    return(ret);
}

/* HELPER - LOADS cacheval
 *
 * This function creates a SID_cacheval_t struct with the
 * attributes that are passed to
 * this function by dynamically creating the SID_cacheval_t.
 * The url attribute cannot
 * be null otherwise the SID_cacheval_t is not dynamically created
 *
 * Returns 0 on success, otherwise -1 is returned
 */
int SID_create_SID_cacheval_t(SID_cacheval_t **cacheval_t,
                              int sid_attributes[],
                              BMI_addr sid_bmi,
                              char *sid_url)
{
    if(!sid_url)
    {
        gossip_err("The url passed to SID_create_SID_cacheval_t is NULL\n");
        *cacheval_t = NULL;
        return(-1);
    }

    /* Mallocing space for the SID_cacheval_t struct */
    *cacheval_t = (SID_cacheval_t *)malloc(sizeof(SID_cacheval_t) +
                                          (strlen(sid_url) + 1));
    SID_initialize_SID_cacheval_t(cacheval_t);
    
    /* Setting the values of the SID_cacheval_t struct */    
    memcpy((*cacheval_t)->attr, sid_attributes, (sizeof(int) * SID_NUM_ATTR));
    memcpy(&((*cacheval_t)->bmi_addr), &sid_bmi, sizeof(BMI_addr));
    strncpy((*cacheval_t)->url, sid_url, (strlen(sid_url) + 1));

    return(0);
}

/*
 * This function clean up a SID_cacheval_t struct by freeing the dynamically
 * created SID_cacheval_t struct
 */
void SID_clean_up_SID_cacheval_t(SID_cacheval_t **cacheval_t)
{
    free(*cacheval_t);
}

/** SID_STORE
 *
 * This function dumps the contents of the sid cache in ASCII to the file
 * specified through the outpfile parameter parameter
 *
 * Returns 0 on success, otherwise returns error code
 */
int SID_dump_sid_cache_to_file(DB **dbp, const char *file_name, int db_records)
{
    int ret = 0;                   /* Function return value */
    int i = 0;                     /* Loop index variable */
    FILE *outpfile;                /* file ptr for output file */
    int attr_pos_index = 0;        /* Index of the attribute from the */
                                   /*     file in the SID_cacheval_t attrs */
    DBC *cursorp;                  /* Database cursor used to iterate */
                                   /*     over contents of the database */
    DBT key, data;          	   /* Database variables */
    char *ATTRS = "ATTRS: ";       /* Tag for the number of attributes on */
                                   /*     the first line of dump file */
    char *SIDS = "SIDS: ";         /* Tag for the number of sids on the */
                                   /*     first line of the dump file */
    char tmp_sid_str[SID_STR_LEN]; /* Temporary string to hold the string */
                                   /*     representation of the sids */
    PVFS_SID tmp_sid;              /* Temporary SID to hold contents of */
                                   /*     the database */
    SID_cacheval_t *tmp_sid_attrs; /* Temporary SID_cacheval_t struct to */
                                   /*     hold contents of database */

    /* Opening the file to dump the contents of the database to */
    outpfile = fopen(file_name, "w");
    if(!outpfile)
    {
        gossip_err("Error opening dump file in SID_dump_sid_cache function\n");
        return(-1);
    }


    /* Creating a cursor to iterate over the database contents */
    ret = (*dbp)->cursor(*dbp,     /* Primary database pointer */
                         NULL,     /* Transaction handle */
                         &cursorp, /* Database cursor that is created */
                         0);       /* Cursor create flags */

    if(ret)
    {
        gossip_err("Error occured when trying to create cursor in \
                    SID_dump_sid_cache function: %s", db_strerror(ret));
        return(ret);
    }

    /* Checking to make sure that there is at lease */
    /* one valid attributes in the file */
    if(valid_attrs_in_file)
    {
        /* Writing the number of attributes in the */
        /* cache to the dump file's first line */
        fprintf(outpfile, "%s", ATTRS);
        fprintf(outpfile, "%d\t", valid_attrs_in_file);

        /* Writing the number of sids in the cache */
        /* to the dump file's first line */
        fprintf(outpfile, "%s", SIDS);
        fprintf(outpfile, "%d\n", db_records);
	
        /* Writing the string representation of the */
        /* attributes in the sid cache to
           the dump file's second line */
        for(i = 0; i < attrs_in_file; i++)
        {
            if(attr_positions[attr_pos_index] != -1)
            {
                fprintf(outpfile, "%s ",
                        SID_attr_map[attr_positions[attr_pos_index]]);
            }
            attr_pos_index++;
        }
        /* Resetting the attribute position index */
        /* to 0 so the next SID_cacheval_t will have */
        /* its attributes in the correct positions in the attrs array */
        attr_pos_index = 0;
        fprintf(outpfile, "%c", '\n');

        /* Initializing the database variables */
        SID_zero_dbt(&key, &data, NULL);

        /* Iterating over the database to get the sids */
        while(cursorp->get(cursorp, &key, &data, DB_NEXT) == 0)
        {
            SID_unpack_SID_cacheval_data(&tmp_sid_attrs, &data);
        
            PVFS_SID_cpy(&tmp_sid, (PVFS_SID *)key.data);
            PVFS_SID_bin2str(&tmp_sid, tmp_sid_str);            

            /* Writing the sids and their attributes to the dump file */
            for(i = 0; i < attrs_in_file; i++)
            {
                if(attr_positions[attr_pos_index] != -1)
                {
                    fprintf(outpfile, "%d ",
                           tmp_sid_attrs->attr[attr_positions[attr_pos_index]]);
                }
                attr_pos_index++;
            }
            /* Resetting the attribute position index
               to 0 so the next SID_cacheval_t
               will have its attributes in the correct
               positions in the attrs array */
            attr_pos_index = 0;

            fprintf(outpfile, "%d ", tmp_sid_attrs->bmi_addr);
            fprintf(outpfile, "%s ", tmp_sid_attrs->url);
            fprintf(outpfile, "%s\n", tmp_sid_str);
 
            SID_clean_up_SID_cacheval_t(&tmp_sid_attrs);
        }
    }
    /* If there was not single valid attribute in the input file, then all
     * attributes will be written out to the dump file 
     */
    else
    {
        /* Writing the number of attributes in
         * the cache to the dump file's first line 
         */
        fprintf(outpfile, "%s", ATTRS);
        fprintf(outpfile, "%d\t", SID_NUM_ATTR);

        /* Writing the number of sids in the cache
         * to the dump file's first line 
         */
        fprintf(outpfile, "%s", SIDS);
        fprintf(outpfile, "%d\n", db_records);
    
        /* Writing the string representation of
         * the attributes in the sid cache to
         * the dump file's second line 
         */
        for(i = 0; i < SID_NUM_ATTR; i++)
        {
           fprintf(outpfile, "%s ", SID_attr_map[i]);
        }
        fprintf(outpfile, "%c", '\n');

        /* Initializing the database variables */
        SID_zero_dbt(&key, &data, NULL);

        /* Iterating over the database to get the sids */
        while(cursorp->get(cursorp, &key, &data, DB_NEXT) == 0)
        {
            SID_unpack_SID_cacheval_data(&tmp_sid_attrs, &data);
        
            PVFS_SID_cpy(&tmp_sid, (PVFS_SID *)key.data);
            PVFS_SID_bin2str(&tmp_sid, tmp_sid_str);            

            /* Writing the sids and their attributes to the dump file */
            for(i = 0; i < SID_NUM_ATTR; i++)
            {
                fprintf(outpfile, "%d ", tmp_sid_attrs->attr[i]);      
            }

            fprintf(outpfile, "%d ", tmp_sid_attrs->bmi_addr);
            fprintf(outpfile, "%s ", tmp_sid_attrs->url);
            fprintf(outpfile, "%s\n", tmp_sid_str);
 
            SID_clean_up_SID_cacheval_t(&tmp_sid_attrs);
        }
    }

    ret = cursorp->close(cursorp);
    if(ret)
    {
        gossip_debug(GOSSIP_SIDCACHE_DEBUG,
            "Error closing cursor in SID_dump_sid_cache function: %s\n",
            db_strerror(ret));
        return(ret);
    }

    fclose(outpfile);
    
    return(ret);
}


/*
 * This function retrieves entries from a primary database and stores
 * them into a bulk buffer DBT. 
 *
 * The minimum amount that can be retrieved is 8 KB of entries.
 * 
 * You must specify a size larger than 8 KB by putting an amount into the
 * two size parameters.
 *
 * The output DBT is malloc'ed so take care to make sure it is freed,
 * either by using it in a bulk_insert or by manually freeing.
 *
 * If the cache is larger than the buffer, the entry that does not fit is
 * saved in bulk_next_key global variable.
*/
int SID_bulk_retrieve_from_sid_cache(int size_of_retrieve_kb,
                                     int size_of_retrieve_mb,
                                     DB *dbp,
                                     DBC **dbcursorp,
                                     DBT *output)
{
    int ret = 0, size;
    DBT key = bulk_next_key;

    /* If the input size of the retrieve is
     * smaller than the minimum size then exit function with error 
     */
    if (BULK_MIN_SIZE > (size_of_retrieve_kb * KILOBYTE) +
                        (size_of_retrieve_mb * MEGABYTE))
    {
        gossip_debug(GOSSIP_SIDCACHE_DEBUG,
                  "Size of bulk retrieve buffer must be greater than 8 KB\n");
        return (-1);
    }

    /* If cursor is open, close it so we can reopen it as a bulk cursor */
    if (*dbcursorp != NULL)
    {
        (*dbcursorp)->close(*dbcursorp);
    }

    /* Calculate size of buffer as size of kb + size of mb */
    size = (size_of_retrieve_kb * KILOBYTE) + (size_of_retrieve_mb * MEGABYTE);

    /* Malloc buffer DBT */
    output->data = malloc(size);
    if (output->data == NULL)
    {
        gossip_debug(GOSSIP_SIDCACHE_DEBUG, "Error sizing buffer\n");
        return (-1);
    }

    output->ulen = size;
    output->flags = DB_DBT_USERMEM;

    /* Open bulk cursor */
    if ((ret = dbp->cursor(dbp, NULL, dbcursorp, DB_CURSOR_BULK)) != 0) {
        free(output->data);
        gossip_debug(GOSSIP_SIDCACHE_DEBUG, "Error creating bulk cursor\n");
        return (ret);
    }

    /* Get items out of db as long as no error,
     * if error is not end of entries in db then save 
     * last unstored entry into global bulk_next_key
     */
    while(ret == 0)
    {
        ret = (*dbcursorp)->get(*dbcursorp,
                                &key,
                                output,
                                DB_MULTIPLE_KEY | DB_NEXT);
        if (ret != 0 && ret != DB_NOTFOUND)
        {
            bulk_next_key = key;
        }
    }

    (*dbcursorp)->close(*dbcursorp);

    return ret;
}


/*
 * This function inserts entries from the input bulk buffer DBT into a database.
 *
 * The function uses the output from SID_bulk_retrieve as its input.
 *
 * The malloc'ed bulk buffer DBT's are freed at the end of this function.
 */
int SID_bulk_insert_into_sid_cache(DB *dbp, DBT *input)
{
    int ret;
    void *opaque_p, *op_p;
    DBT output;
    size_t retklen, retdlen;
    void *retkey, *retdata;

    /* Malloc buffer DBT */
    output.data = malloc(input->size);
    output.ulen = input->size;
    output.flags = DB_DBT_USERMEM;

    /* Initialize bulk DBTs */
    DB_MULTIPLE_WRITE_INIT(opaque_p, &output);
    DB_MULTIPLE_INIT(op_p, input);

    /*
     * Read key and data from input bulk buffer into output bulk buffer
     *
     */
    while(1)
    {
        DB_MULTIPLE_KEY_NEXT(op_p,
                             input,
                             retkey,
                             retklen,
                             retdata,
                             retdlen);
        if (op_p == NULL)
        {
            break;
        }

        DB_MULTIPLE_KEY_WRITE_NEXT(opaque_p,
                                   &output,
                                   retkey,
                                   retklen,
                                   retdata,
                                   retdlen);
        if (opaque_p == NULL)
        {
            gossip_debug(GOSSIP_SIDCACHE_DEBUG,
                           "Error cannot fit into write buffer\n");
            break;
        }
    }

    /* Bulk insert of output bulk buffer */
    ret = dbp->put(dbp,
                   NULL,
                   &output,
                   NULL,
                   DB_MULTIPLE_KEY | DB_OVERWRITE_DUP);

    /* Free both bulk buffers */
    free(input->data);
    free(output.data);

    return ret;
}





/* <======================== DATABASE FUNCTIONS ==========================> */
/*
 * This function zeros out the DBT's and should be used before any
 * element is placed into the database
 */
void SID_zero_dbt(DBT *key, DBT *data, DBT *pkey)
{
    if(key != NULL)
    {
        memset(key, 0, sizeof(DBT));
    }
    if(data != NULL)
    {
        memset(data, 0, sizeof(DBT));
    }
    if(pkey != NULL)
    {
        memset(pkey, 0, sizeof(DBT));
    }
}

/*
 * This function creates and opens the environment handle
 *
 * Returns 0 on success, otherwise returns an error code.
 */
int SID_create_open_environment(DB_ENV **envp)
{
    int ret = 0;

    /* Setting the opening environement flags */
    u_int32_t flags =
        DB_CREATE     | /* Creating environment if it does not exist */
        DB_INIT_MPOOL | /* Initializing the memory pool (in-memory cache) */
        DB_PRIVATE;     /* Region files are not backed up by the file system
                           instead they are backed up by heap memory */

    /* Creating the environment. Returns 0 on success */
    ret = db_env_create(envp, /* Globacl environment pointer */
                        0);    /* Environment create flag (Must be set to 0) */

    if(ret)
    {
        gossip_err("Error creating environment handle : %s\n", db_strerror(ret));
        return(ret);
    }

    /* Setting the in cache memory size. Returns 0 on success */
    ret = (*envp)->set_cachesize(
                        /* Environment pointer */
                        *envp,
                        /* Size of the cache in GB. Defined in sidcache.h */
                        CACHE_SIZE_GB,
                        /* Size of the cache in MB. Defined in sidcache.h */
                        CACHE_SIZE_MB * MEGABYTE,
                        /* Number of caches to create */
                        1);

    if(ret)
    {
	gossip_err(
              "Error setting cache memory size for environment : %s\n",
              db_strerror(ret));
        return(ret);
    }

    /* Opening the environment. Returns 0 on success */
    ret = (*envp)->open(*envp, /* Environment pointer */
                        NULL,  /* Environment home directory */
                        flags, /* Environment open flags */
                        0);    /* Mmode used to create BerkeleyDB files */

    if(ret)
    {
        gossip_err("Error opening environment database handle : %s\n",
                   db_strerror(ret));
        return(ret);
    }

    return(ret);
}

/*
 * This function creates and opens the primary database handle, which
 * has DB_HASH as the access method
 *
 * Returns 0 on success. Otherwise returns error code.
 */
int SID_create_open_sid_cache(DB_ENV *envp, DB **dbp)
{
    int ret = 0;

    u_int32_t flags = DB_CREATE; /* Database open flags. Creates the database if
                                    it does not already exist */

    /* Setting the global bulk next key to zero */
    SID_zero_dbt(&bulk_next_key, NULL, NULL);

    ret = db_create(dbp,   /* Primary database pointer */
                    envp,  /* Environment pointer */
                    0);    /* Create flags (Must be set to 0 or DB_XA_CREATE) */
    if(ret)
    {
        gossip_err("Error creating handle for database : %s\n",
                   db_strerror(ret));
        return(ret);
    }

    ret = (*dbp)->open(*dbp,    /* Primary database pointer */
                       NULL,    /* Transaction pointer */
                       NULL,    /* On disk file that holds database */
                       NULL,    /* Optional logical database */
                       DB_HASH, /* Database access method */
                       flags,   /* Database opening flags */
                       0);      /* File mode. Default is 0 */
   if(ret)
   {
        gossip_err("Error opening handle for database : %s\n",
                   db_strerror(ret));
        return(ret);
   }

   return(ret);
}

/* 
 * This function creates, opens, and associates the secondary attribute
 * database handles and sets the database pointers in the secondary_dbs 
 * array to point at the correct database. The acccess method for the
 * secondary databases is DB_BTREE and allows for duplicate records with
 * the same key values
 *
 * Returns 0 on success, otherwise returns an error code
 */
int SID_create_open_assoc_sec_dbs(
                        DB_ENV *envp,
                        DB *dbp,
                        DB *secondary_dbs[], 
                        int (* key_extractor_func[])(DB *pri,
                                                     const DBT *pkey,
                                                     const DBT *pdata,
                                                     DBT *skey))
{
    int ret = 0;
    int i = 0;
    DB *tmp_db = NULL;
    u_int32_t flags = DB_CREATE;

    ret = SID_initialize_secondary_dbs(secondary_dbs);
    if(ret)
    {
        gossip_err("Could not initialize secondary atttribute db array\n");
        return(ret);
    }

    for(i = 0; i < SID_NUM_ATTR; i++)
    {
        /* Creating temp database */
        ret = db_create(&tmp_db, /* Database pointer */
                        envp,    /* Environment pointer */
                        0);      /* Create flags (Must be 0 or DB_XA_CREATE) */
        if(ret)
        {
            gossip_err("Error creating handle for database :  %s\n",
                       db_strerror(ret));
            return(ret);
        }

        /* Pointing database array pointer at secondary attribute database */
        secondary_dbs[i] = tmp_db;

        /* Setting opening flags for secondary database to allow duplicates */
        ret = tmp_db->set_flags(tmp_db, DB_DUPSORT);
        if(ret)
        {
            gossip_err("Error setting duplicate flag for database : %s\n",
                       db_strerror(ret));
            return(ret);
        }

        /* Opening secondary database and setting its type as DB_BTREE */
        ret = tmp_db->open(tmp_db,   /* Database pointer */
                           NULL,     /* Transaction pointer */
                           NULL,     /* On disk file that holds database */
                           NULL,     /* Optional logical database */
                           DB_BTREE, /* Database access method */
                           flags,    /* Open flags */
                           0);       /* File mode. 0 is the default */

        if(ret)
        {
            gossip_err("Error opening handle for database : %s\n",
                       db_strerror(ret));
            return(ret);
        }

        /* Associating the primary database to the secondary.
           Returns 0 on success */
        ret = dbp->associate(dbp,                      /* Primary db ptr */
                             NULL,                     /* TXN id */
                             tmp_db,                   /* Secondary db ptr */
                             key_extractor_func[i],    /* key extractor func */
                             0);                       /* Associate flags */

        if(ret)
        {
            gossip_err("Error associating the two databases %s\n",
                        db_strerror(ret));
            return(ret);
        }

    }

   return(0);
}

/* 
 * This function creates and opens the database cursors set to the secondary
 * attribute databases in the database cursor pointers array
 *
 * Returns 0 on success, otherwise returns an error code
 */
int SID_create_open_dbcs(DB *secondary_dbs[], DBC *db_cursors[])
{
    int i = 0;                /* Loop index variable */
    int  ret = 0;             /* Function return value */
    DBC *tmp_cursorp = NULL;  /* BerkeleyDB cursor used for opening
                                 cursors for all secondary databases */

    /* Initializing all the database cursors in the array to be
     * set to NULL 
     */
    ret = SID_initialize_database_cursors(db_cursors);
    if(ret)
    {
        gossip_err("Error initializing cursor array :  %s\n", db_strerror(ret));
        return(ret);
    }

    for(i = 0; i < SID_NUM_ATTR; i++)
    {
        ret = secondary_dbs[i]->cursor(
                                secondary_dbs[i], /* Secondary db pointer */
                                NULL,             /* TXN id */
                                &tmp_cursorp,     /* Cursor pointer */
                                0);               /* Cursor opening flags */
        
        /* Checking to make sure the cursor was created */
        if(ret)
        {
            gossip_err("Error creating cursor :  %s\n", db_strerror(ret));
            return(ret);
        }
        /* If the cursor was successfully created it is added to the
         * array of database cursors 
         */
        else
        {
            db_cursors[i] = tmp_cursorp;
        }
    }
    return(ret);
}

/*
 * This function closes the database cursors in the cursors pointer array
 *
 * On success 0 is returned, otherwise returns an error code
 */
int SID_close_dbcs(DBC *db_cursors[])
{
    int i = 0;
    int ret = 0;

    for(i = 0; i < SID_NUM_ATTR; i++)
    {
        /* If the cursor is opened then it is closed */
        if(db_cursors[i] != NULL)
        {
            ret = db_cursors[i]->close(db_cursors[i]);
            if(ret)
            {
                gossip_debug(GOSSIP_SIDCACHE_DEBUG,
                             "Error closing cursor :  %s\n", db_strerror(ret));
                return(ret);
            }
        }
    }
    return(ret);
}

/*
 * This function closes the primary database, secondary attribute databases,
 * and environment handles
 *
 * Returns 0 on success, otherwisre returns an error code
 */
int SID_close_dbs_env(DB_ENV *envp, DB *dbp, DB *secondary_dbs[])
{
    int ret = 0;
    int i = 0;

    /* Closing the array of secondary attribute databases */ 
    for(i = 0; i < SID_NUM_ATTR; i++)
    {
        if(secondary_dbs[i] != NULL)
        {

            ret = secondary_dbs[i]->close(secondary_dbs[i], /*Secondary db ptr*/
                                          0);         /* Database close flags */

            /* Checking to make sure the secondary database has been closed */
            if(ret)
            {
                gossip_debug(GOSSIP_SIDCACHE_DEBUG,
                             "Error closing attribute database : %s\n",
                             db_strerror(ret));
                return(ret);
            }
        }
    }

    /* Checking to make sure that database handle has been opened */
    if(dbp != NULL)
    {
        /* Closing the primary database. Returns 0 on success */
        ret = dbp->close(dbp, /* Primary database pointer */
                         0);  /* Database close flags */
    }

     if(ret)
    {
        gossip_debug(GOSSIP_SIDCACHE_DEBUG,
                     "Error closing primary database : %s\n", db_strerror(ret));
        return(ret);
    }


    /* Checking to make sure the environment handle is open */
    if(envp != NULL)
    {
        ret  = envp->close(envp, /* Environment pointer */
                           0);   /* Environment close flags */
    }

    if(ret)
    {
        gossip_debug(GOSSIP_SIDCACHE_DEBUG,
                     "Error closing environment :  %s\n",
                     db_strerror(ret));
        return(ret);
    }

    /* Freeing the attr_position array which is used for loading and
     * dumping the contents of the sid cache 
     */
    if(attr_positions != NULL)
    {
        free(attr_positions);
    }

    return(ret);
}

/* <======================== EXPORTED FUNCTIONS ==========================> */

/* called from startup routines, this gets the SID cache up and running
 * should work on server and client
 */
int SID_initialize(void)
{
    int ret = -1;
    static int initialized = 0;

    /* if already initialized bail out - we assume higher levels
     * prevent multiple threads from initializing so we won't deal
     * with that here.
     */
    if (initialized)
    {
        ret = 0;
        goto errorout;
    }

    /* create DBD env */
    ret = SID_create_open_environment(&SID_envp);
    if (ret < 0)
    {
        goto errorout;
    }

    /* create main DB */
    ret = SID_create_open_sid_cache(SID_envp, &SID_db);
    if (ret < 0)
    {
        goto errorout;
    }

    /* create secondary (attribute index) DBs */
    ret = SID_create_open_assoc_sec_dbs(SID_envp,
                                        SID_db,
                                        SID_attr_index,
                                        SID_extract_key);
    if (ret < 0)
    {
        goto errorout;
    }

    /* create cursors */
    ret = SID_create_open_dbcs(SID_attr_index, SID_attr_cursor);
    if (ret < 0)
    {
        SID_close_dbs_env(SID_envp, SID_db, SID_attr_index);
        goto errorout;
    }

    /* load entries from the configuration */

errorout:
    return ret;
}

/* called to load the contents of the SID cache from a file
 * so we do not have to discover everything
 */
int SID_load(void)
{
    int ret = -1;
    int records_imported = 0;
    char *filename = NULL;
    struct server_configuration_s *srv_conf;
    int fnlen;
    struct stat sbuf;
    PVFS_fs_id fsid __attribute__ ((unused)) = PVFS_FS_ID_NULL;

    /* figure out the path to the cached data file */
    srv_conf = PINT_server_config_mgr_get_config(fsid);
    fnlen = strlen(srv_conf->meta_path) + strlen("/SIDcache");
    filename = (char *)malloc(fnlen + 1);
    strncpy(filename, srv_conf->meta_path, fnlen + 1);
    strncat(filename, "/SIDcache", fnlen + 1);
    PINT_server_config_mgr_put_config(srv_conf);

    /* check if file exists */
    ret = stat(filename, &sbuf);
    if (ret < 0)
    {
        if (errno == EEXIST)
        {
            /* file doesn't exist - not an error, but bail out */
            errno = 0;
            ret = 0;
        }
        goto errorout;
    }

    /* load cache from file */
    ret = SID_load_sid_cache_from_file(&SID_db,
                                       filename,
                                       &records_imported);
    if (ret < 0)
    {
        /* something failed, close up the database */
        SID_close_dbcs(SID_attr_cursor);
        SID_close_dbs_env(SID_envp, SID_db, SID_attr_index);
        goto errorout;
    }

errorout:
    return ret;
}

/* called periodically to save the contents of the SID cache to a file
 * so we can reload at some future startup and not have to discover
 * everything
 */
int SID_save(void)
{
    int ret = -1;
    int records_exported = 0;
    char *filename = NULL;
    struct server_configuration_s *srv_conf;
    int fnlen;
    PVFS_fs_id fsid __attribute__ ((unused)) = PVFS_FS_ID_NULL;

    /* figure out the path to the cached data file */
    srv_conf = PINT_server_config_mgr_get_config(fsid);
    fnlen = strlen(srv_conf->meta_path) + strlen("/SIDcache");
    filename = (char *)malloc(fnlen + 1);
    strncpy(filename, srv_conf->meta_path, fnlen + 1);
    strncat(filename, "/SIDcache", fnlen + 1);
    PINT_server_config_mgr_put_config(srv_conf);

    /* dump cache to the file */
    ret = SID_dump_sid_cache_to_file(&SID_db, filename, records_exported);
    if (ret < 0)
    {
        return ret;
    }

    return ret;
}

/* called from shutdown routines, this closes up the BDB constructs
 * and saves the contents to a file
 */
int SID_finalize(void)
{
    int ret = -1;

    /* close cursors */
    ret = SID_close_dbcs(SID_attr_cursor);
    if (ret < 0)
    {
        return ret;
    }

    /* close DBs and env */
    ret = SID_close_dbs_env(SID_envp, SID_db, SID_attr_index);
    if (ret < 0)
    {
        return ret;
    }

    return ret;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */