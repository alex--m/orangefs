/* 
 * (C) 2001 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

#include "str-utils.h"

/* PINT_string_count_segments()
 *
 * Count number of segments in a path.
 *
 * Parameters:
 * pathname   - pointer to string
 *
 * Returns number of segments in pathname; -1 if
 * pathname is invalid or has no components
 *
 * Example inputs and return values:
 *
 * NULL              - returns -1
 * /                 - returns -1
 * filename          - returns  1
 * /filename         - returns  1
 * /filename/        - returns  1
 * /filename//       - returns  1
 * /dirname/filename - returns  2
 * dirname/filename
 *
 */
int PINT_string_count_segments(char *pathname)
{
    int count = 0;
    char *segp = (char *)0;
    void *segstate;

    while(!PINT_string_next_segment(pathname,&segp,&segstate))
    {
        count++;
    }
    return count;
}

/* PINT_get_base_dir()
 *
 * Get base (parent) dir of a absolute path
 *
 * Parameters:
 * pathname     - pointer to directory string
 * out_base_dir - pointer to out base dir string
 * max_out_len  - max length of out_base_dir buffer
 *
 * All incoming arguments must be valid and non-zero
 *
 * Returns 0 on success; -1 if args are invalid
 *
 * Example inputs and outputs/return values:
 *
 * pathname: /tmp         - out_base_dir: /         - returns  0
 * pathname: /tmp/foo     - out_base_dir: /tmp      - returns  0
 * pathname: /tmp/foo/bar - out_base_dir: /tmp/foo  - returns  0
 *
 *
 * invalid pathname input examples:
 * pathname: /            - out_base_dir: undefined - returns -1
 * pathname: NULL         - out_base_dir: undefined - returns -1
 * pathname: foo          - out_base_dir: undefined - returns -1
 *
 */
int PINT_get_base_dir(char *pathname, char *out_base_dir, int out_max_len)
{
    int ret = -1, len = 0;
    char *start, *end;

    if (pathname && out_base_dir && out_max_len)
    {
        if ((strcmp(pathname,"/") == 0) || (pathname[0] != '/'))
        {
            return ret;
        }

        start = pathname;
        end = (char *)(pathname + strlen(pathname));

        while(end && (end > start) && (*(--end) != '/'));

        /*
          get rid of trailing slash unless we're handling
          the case where parent is the root directory
          (in root dir case, len == 1)
        */
        len = ++end - start;
        if (len != 1)
        {
            len--;
        }
        if (len < out_max_len)
        {
            memcpy(out_base_dir,start,len);
            out_base_dir[len] = '\0';
            ret = 0;
        }
    }
    return ret;
}

/* PINT_string_next_segment()
 *
 * Parameters:
 * pathname   - pointer to string
 * inout_segp - address of pointer; NULL to get first segment,
 *              pointer to last segment to get next segment
 * opaquep    - address of void *, used to maintain state outside
 *              the function
 *
 * Returns 0 if segment is available, -1 on end of string.
 *
 * This approach is nice because it keeps all the necessary state
 * outside the function and concisely stores it in two pointers.
 *
 * Internals:
 * We're using opaquep to store the location of where we placed a
 * '\0' separator to get a segment.  The value is undefined when
 * called with *inout_segp == NULL.  Afterwards, if we place a '\0',
 * *opaquep will point to where we placed it.  If we do not, then we
 * know that we've hit the end of the path string before we even
 * start processing.
 *
 * Note that it is possible that *opaquep != NULL and still there are
 * no more segments; a trailing '/' could cause this, for example.
 */
int PINT_string_next_segment(char *pathname,
                             char **inout_segp,
                             void **opaquep)
{
    char *ptr = (char *)0;

    /* initialize our starting position */
    if (*inout_segp == NULL) {
	ptr = pathname;
    }
    else if (*opaquep != NULL) {
	/* replace the '/', point just past it */
	ptr = (char *) *opaquep;
	*ptr = '/';
	ptr++;
    }
    else return -1; /* NULL *opaquep indicates last segment returned last time */

    /* at this point, the string is back in its original state */

    /* jump past separators */
    while ((*ptr != '\0') && (*ptr == '/')) ptr++;
    if (*ptr == '\0') return -1; /* all that was left was trailing '/'s */

    *inout_segp = ptr;

    /* find next separator */
    while ((*ptr != '\0') && (*ptr != '/')) ptr++;
    if (*ptr == '\0') *opaquep = NULL; /* indicate last segment */
    else {
	/* terminate segment and save position of terminator */
	*ptr = '\0';
	*opaquep = ptr;
    }
    return 0;
}


/*
 * PINT_parse_handle_ranges:  the first time this is called, set 'status' to
 * zero.  get back the first range in the string in the out_extent
 * variable.  keep calling PINT_parse_handle_ranges until it returns zero
 *
 * range            :  string representing our ranges
 * out_extent->first:  (output) beginning of range
 * out_extent->last :  (output) end of range
 * status           :  (opaque) how far we are in the range string 
 *
 * returns:
 *  0: no more ranges
 *  1: found a range.  look at 'first' and 'last' for the values
 *  -1: something bad happened, possibly invalid arguments
 */
int PINT_parse_handle_ranges(
    char *range, 
    PVFS_handle_extent *out_extent,
    int *status)
{ 
    char *p = NULL, *endchar = NULL;

    if (!out_extent || !status)
    {
        return -1;
    }

    p = range + *status;

    /* from strtol(3):
       If endptr is not NULL, strtoul() stores the address of the
       first invalid character in *endptr.  If there were no dig�
       its at all, strtoul() stores the original value of nptr in
       *endptr  (and  returns 0).  In particular, if *nptr is not
       `\0' but **endptr is `\0' on return, the entire string  is
       valid.  */ 
    out_extent->first = out_extent->last =
#ifdef HAVE_STRTOULL
        (PVFS_handle)strtoull(p, &endchar, 0);
#else
        (PVFS_handle)strtoul(p, &endchar, 0);
#endif
    if ( p == endchar )  /* all done */
	return 0; 
    /* strtoul eats leading space, but not trailing space.  take care of ws
     * between number and delimiter (- or ,) */
    while (isspace(*endchar)) endchar++; 
    
    p = endchar+1; /* skip over the ',' or '-'*/

    switch (*endchar) {
	case '-': /* we got the first half of the range. grab 2nd half */
#ifdef HAVE_STRTOULL
	    out_extent->last = (PVFS_handle)strtoull(p, &endchar, 0);
#else
	    out_extent->last = (PVFS_handle)strtoul(p, &endchar, 0);
#endif
	    /* again, skip trailing space ...*/
	    while (isspace(*endchar)) endchar++;
	    /* ... and the delimiter */ 
	    if (*endchar == ',') endchar ++;
	    /* 'status' tells us how far we are in the string */
	    *status = ( endchar - range);
	    break; 
	case ',': /* end of a range */
	case '\0': /* end of the whole string */
	    *status = ( p - range );
	    break;
	default:
	    return -1;
    }
    return 1;
}

/*
 * PINT_get_path_element:  gets the specified segment in the
 * provided path.
 *
 * pathname   :  string containing a valid pathname
 * segment_num:  the desired segment number in the path
 * out_segment:  where the segment will be stored on success
 * out_max_len:  max num bytes to store in out_segment
 *
 * returns:
 *  0 : if the segment was found and copied
 *  -1: if an invalid segment was specified
 *
 * Example inputs and outputs/return values:
 *
 * pathname: /mnt/pvfs2/foo, segment_num: 0
 *     out_segment: mnt, returns 0
 * pathname: /mnt/pvfs2/foo, segment_num: 2
 *     out_segment: foo, returns 0
 * pathname: /mnt/pvfs2/foo, segment_num: 5
 *     out_segment: undefined, returns -1
 */
int PINT_get_path_element(
    char *pathname,
    int segment_num,
    char *out_segment,
    int out_max_len)
{
    int count = -1;
    char *segp = (char *)0;
    void *segstate;
    char local_pathname[PVFS_NAME_MAX] = {0};

    strncpy(local_pathname,pathname,PVFS_NAME_MAX);

    while(!PINT_string_next_segment(local_pathname,&segp,&segstate))
    {
        if (++count == segment_num)
        {
            strncpy(out_segment,segp,(size_t)out_max_len);
            break;
        }
    }
    return ((count == segment_num) ? 0 : -1);
}

/* PINT_get_next_path
 *
 * gets remaining path given number of path segments to skip
 *
 * returns 0 on success, -errno on failure
 */
int PINT_get_next_path(char *path, char **newpath, int skip)
{
    int pathlen=0, i=0, num_slashes_seen=0;
    int delimiter1=0;

    pathlen = strlen(path) + 1;

    /* find our starting point in the old path, it could be past multiple 
     * segments*/
    for(i =0; i < pathlen; i++)
    {
	if (path[i] == '/')
	{
	    num_slashes_seen++;
	    if (num_slashes_seen > skip)
	    {
		break;
	    }
	}
    }

    delimiter1 = i;
    if (pathlen - delimiter1 < 1)
    {
        return (-EINVAL);
    }

    *newpath = malloc(pathlen - delimiter1);
    if (*newpath == NULL)
    {
        return (-ENOMEM);
    }
    memcpy(*newpath, &path[delimiter1], pathlen - delimiter1 );
    /* *newpath[pathlen - delimiter1 -1 ] = '\0';*/
    return(0);
}

/*
 * PINT_split_string_list()
 *
 * separates a comma delimited list of items into an array of strings
 *
 * returns the number of strings successfully parsed
 */
int PINT_split_string_list(char ***tokens, const char *comma_list)
{

    const char *holder = NULL;
    const char *holder2 = NULL;
    const char *end = NULL;
    int tokencount = 1;
    int i = -1;

    if (!comma_list || !tokens)
    {
	return (0);
    }

    /* count how many commas we have first */
    holder = comma_list;
    while ((holder = index(holder, ',')))
    {
	tokencount++;
	holder++;
    }

    /* allocate pointers for each */
    *tokens = (char **) malloc(sizeof(char **));
    if (!(*tokens))
    {
	return 0;
    }

    /* copy out all of the tokenized strings */
    holder = comma_list;
    end = comma_list + strlen(comma_list);
    for (i = 0; i < tokencount; i++)
    {
	holder2 = index(holder, ',');
	if (!holder2)
	{
	    holder2 = end;
	}
	(*tokens)[i] = (char *) malloc((holder2 - holder) + 1);
	if (!(*tokens)[i])
	{
	    goto failure;
	}
	strncpy((*tokens)[i], holder, (holder2 - holder));
	(*tokens)[i][(holder2 - holder)] = '\0';
	holder = holder2 + 1;

    }

    return (tokencount);

  failure:

    /* free up any memory we allocated if we failed */
    if (*tokens)
    {
	for (i = 0; i < tokencount; i++)
	{
	    if ((*tokens)[i])
	    {
		free((*tokens)[i]);
	    }
	}
	free(*tokens);
    }
    return (0);
}

#ifndef HAVE_STRNLEN
/* a naive implementation of strnlen for systems w/o glibc */
size_t strnlen(const char *s, size_t limit)
{
   size_t len = 0;
   while ((len < limit) && (*s++))
     len++;
   return len;
}
#endif

#ifndef HAVE_STRSTR
/* a custom implementation of strstr for systems w/o it */
char *strstr(const char *haystack, const char *needle)
{
    char *ptr = NULL;
    int needle_len = 0;
    int remaining_len = 0;

    ptr = (char *)haystack;
    if (haystack && needle)
    {
        needle_len = strlen(needle);
        remaining_len = strlen(haystack);

        while(ptr)
        {
            if (*ptr == *needle)
            {
                if (memcmp(ptr, needle, needle_len) == 0)
                {
                    break;
                }
            }
            ptr++;
            remaining_len--;

            if ((remaining_len < needle_len) || (*ptr == '\0'))
            {
                ptr = NULL;
                break;
            }
        }
    }
    return ptr;
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


