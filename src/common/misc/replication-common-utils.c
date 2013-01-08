/* 
 *  (C) 2001 Clemson University 
 *  
 *   See COPYING in top-level directory.
 *  
 */

#include <stdio.h>
#include <string.h>
#include "replication-common-utils.h"
#include "gossip.h"
#include "server-config.h"
#include "pint-sysint-utils.h"


const char *PINT_layout_str_mapping[] = {     \
    "IGNORE", /* 0 */                         \
    "PVFS_SYS_LAYOUT_NONE",        /* 1 */    \
    "PVFS_SYS_LAYOUT_ROUND_ROBIN", /* 2 */    \
    "PVFS_SYS_LAYOUT_RANDOM",      /* 3 */    \
    "PVFS_SYS_LAYOUT_LIST",        /* 4 */    \
};

/* helper function to calculate the TOTAL size of the layout, so we can allocate
 * all of the space as one contiguous chunk.
 *   
 */
int replication_calculate_layout_size( int32_t *layout_size
                                      ,PVFS_sys_layout *replication_layout )
{
   /* size of replication_layout structure */
   *layout_size = sizeof(PVFS_sys_layout);

   /* add in size of the server list, if it is present. */
   if ( replication_layout->algorithm == PVFS_SYS_LAYOUT_LIST )
   {
      *layout_size += replication_layout->server_list.count *
                      sizeof(*replication_layout->server_list.servers);
   }

   gossip_lerr("layout size is %d\n",*(int *)layout_size);

   return(0);
}/*end replication_calculate_layout_size*/


/* this function ASSUMES that all memory has already been allocated */
int replication_copy_layout( PVFS_sys_layout *src
                            ,void *dest )
{
   PVFS_sys_layout *dest_layout = (PVFS_sys_layout *)dest;

   dest_layout->algorithm         = src->algorithm;
   dest_layout->server_list.count = src->server_list.count;

   if ( src->algorithm == PVFS_SYS_LAYOUT_LIST )
   {
      memcpy (  dest_layout->server_list.servers,
                src->server_list.servers,
                src->server_list.count *
                sizeof(*src->server_list.servers)
             );
   }
   else
   {
      dest_layout->server_list.servers = NULL;
   }

   return(0);
}/*end replication_copy_layout*/

/* Return the string value of the layout algorithm associated with its index. */
const char *get_algorithm_string_value ( uint32_t index )
{

   return (PINT_layout_str_mapping[index]);

}/*end get_algorithm_string_value */


/* helper function to copy data from one replication structure to another */
int copy_replication_info(replication_s *src_p, replication_s *dest_p)
{
    int32_t server_count = 0;

    dest_p->replication_switch           = src_p->replication_switch;
    dest_p->replication_number_of_copies = src_p->replication_number_of_copies;

    dest_p->replication_layout.algorithm         = src_p->replication_layout.algorithm;
    dest_p->replication_layout.server_list.count = server_count = src_p->replication_layout.server_list.count;

    if (server_count <= 0)
    {
        return(0);
    }

    dest_p->replication_layout.server_list.servers = calloc(server_count,sizeof(*src_p->replication_layout.server_list.servers));
    if (!dest_p->replication_layout.server_list.servers)
    {
       gossip_err("%s:Error allocating memory for dst->replication_layout.server_list.servers.\n",__func__);
       return(-PVFS_ENOMEM);
    }

    memcpy(dest_p->replication_layout.server_list.servers,
           src_p->replication_layout.server_list.servers,
           server_count * sizeof(*src_p->replication_layout.server_list.servers));

    return(0);

}/*end copy_replication_info*/


/* helper function to print the replication structure */
void print_replication_structure(replication_s *replication_p)
{

    //gossip_debug(GOSSIP_CLIENT_DEBUG,"\nreplication switch : %d.\n"
    gossip_err("replication structure pointer : %p.\n",replication_p);
    gossip_err("           replication switch : %s.\n",replication_p->replication_switch?"ON":"OFF");
    gossip_err("             number of copies : %d.\n",replication_p->replication_number_of_copies);
    gossip_err("                    algorithm : %s.\n",get_algorithm_string_value(replication_p->replication_layout.algorithm));


    if (replication_p->replication_layout.server_list.count != 0)
    {
        int i;
        for (i=0; i<replication_p->replication_layout.server_list.count; i++)
            //gossip_debug(GOSSIP_CLIENT_DEBUG,"replication server[%d] : %d\n"
            gossip_err("replication server[%d] : %d\n"
                      ,i
                      ,(int)replication_p->replication_layout.server_list.servers[i]);
    }

    return;

}/*end print_replication_structure*/

/*
 * Local variables:
 *   c-indent-level: 4
 *   c-basic-offset: 4
 * End:
 *
 * vim:  ts=8 sts=4 sw=4 expandtab
 */                                                    