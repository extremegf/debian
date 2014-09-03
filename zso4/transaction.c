/*
 * Module handles the transactional operations on the database.
 *
 * Author: Przemyslaw Horban <p.horban@mimuw.edu.pl>
 */

#include <linux/list.h>
#include <linux/radix-tree.h>
#include <linux/rwsem.h>
#include <linux/slab.h>
#include <linux/errno.h>

#include "transaction.h"

#define RADIX_TREE_GANG_SIZE 100

typedef size_t ver_t;

struct seg_read {
    size_t seg_nr;
    ver_t ver_id;
};

struct db_seg {
    ver_t ver_id;
    char data[SEGMENT_SIZE];
};

struct db_version {
    struct radix_tree_root segments;
    struct db_version *parent;
    struct list_head all_other;
    char is_parent;  // Not kept fresh at all times!
};

struct trans_context_t {
    ver_t ver_id;
    struct list_head reads;
    struct db_version ver;
};

// DB versions path end. RCU
struct db_version *db_cur_ver;
size_t commits_since_compat;

// Represents infinite number of 0ed segments.
struct db_seg null_seg;

// List of all db version. Used for compaction and cleanup.
struct list_head all_db_vers;

// Next version id.
ver_t next_ver;

// Locks chain traversal, during compaction.
struct rw_semaphore chain_rw_sem;

static struct db_version *new_db_version(struct db_version *parent) {
	struct db_version *ver = kmalloc(GFP_KERNEL, sizeof(struct db_version));
	if (!ver)
		return NULL;

    INIT_RADIX_TREE(&ver->segments, GFP_KERNEL);
    list_add(&ver->all_other, &all_db_vers);
    ver->parent = parent;
    return ver;
}

static void destroy_db_version(struct db_version *ver) {
	size_t indices[RADIX_TREE_GANG_SIZE];
	size_t found, start = 0, i;

	do {
		void **slot;
		struct radix_tree_iter iter;
		found = 0;

		radix_tree_for_each_slot(slot, &ver->segments, &iter, start)	{
			indices[found] = iter.index;
			found += 1;

			if (found == RADIX_TREE_GANG_SIZE) {
				break;
			}
		}

		start = iter.index;

		for (i = 0; i < found; i++) {
			void *db_seg = radix_tree_delete(&ver->segments, indices[i]);
			kfree(db_seg);
		}
	} while(found > 0);

	list_del(&ver->all_other);

	kfree(ver);
}

int trans_init(void) {
	INIT_LIST_HEAD(&all_db_vers);

	db_cur_ver = new_db_version(NULL);
	if (!db_cur_ver)
		return -ENOMEM;

	commits_since_compat = 0;

	memset(&null_seg.data, 0, SEGMENT_SIZE);
	null_seg.ver_id = 0;

	next_ver = 1;

	init_rwsem(&chain_rw_sem);
}

void trans_destroy(void) {
	struct db_version *ver, *n;
	list_for_each_entry_safe(ver, n, &all_db_vers, all_other) {
		destroy_db_version(ver);
	}
}

/* Design

*/

