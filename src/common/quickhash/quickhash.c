/* 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include<stdio.h>
#include<stdlib.h>

#include<quickhash.h>


/* qhash_init()
 *
 * creates a new hash table with the specified table size.  The
 * hash function and compare function must be provided.
 * table_size should be a good prime number.
 *
 * returns pointer to table on success, NULL on failure
 */
struct qhash_table* qhash_init(
	int(*compare)(void* key, struct qlist_head* link), 
	int(*hash)(void* key, int table_size),
	int table_size)
{
	struct qhash_table* new_table = NULL;
	int i = 0;

	/* create struct to contain table information */
	new_table = (struct qhash_table*)malloc(sizeof(struct qhash_table));
	if(!new_table)
	{
		return(NULL);
	}

	/* fill in info */
	new_table->compare = compare;
	new_table->hash = hash;
	new_table->table_size = table_size;

	/* create array for actual table */
	new_table->array = (struct qlist_head*)malloc(sizeof(struct
		qlist_head) * table_size);
	if(!new_table->array)
	{
		free(new_table);
		return(NULL);
	}
	
	/* initialize a doubly linked at each hash table index */
	for(i=0; i<table_size; i++)
	{
		INIT_QLIST_HEAD(&new_table->array[i]);
	}

	return(new_table);
}


/* qhash_finalize()
 *
 * frees any resources created by the hash table
 *
 * no return value
 */
void qhash_finalize(
	struct qhash_table* old_table)
{
	free(old_table->array);
	free(old_table);
	return;
}

/* qhash_add()
 *
 * adds a new link onto the hash table, hashes based on given key
 *
 * no return value
 */
void qhash_add(
	struct qhash_table* table,
	void* key,
	struct qlist_head* link)
{
	int index = 0;

	/* hash on the key */
	index = table->hash(key,table->table_size);

	/* add to the tail of the linked list at that offset */
	qlist_add_tail(link, &(table->array[index]));

	return;
}

/* qlist_search()
 *
 * searches for a link in the hash table that matches the given
 * key
 *
 * returns pointer to link on success, NULL on failure (or item
 * not found)
 */
struct qlist_head* qhash_search(
	struct qhash_table* table,
	void* key)
{
	int index = 0;
	struct qlist_head* tmp_link;

	/* find the hash value */
	index = table->hash(key, table->table_size);

	/* linear search at index to find match */
	qlist_for_each(tmp_link, &(table->array[index]))
	{
		if(table->compare(key, tmp_link))
		{
			return(tmp_link);
		}
	}
	return(NULL);
}

