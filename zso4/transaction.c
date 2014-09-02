/*
 * Module handles the transactional operations on the database.
 *
 * Author: Przemyslaw Horban <p.horban@mimuw.edu.pl>
 */

#include <linux/list.h>
#include <linux/radix-tree.h>
#include <linux/semaphore.h>


#include "transaction.h"

typedef size_t ver_t;

struct seg_read {
    size_t seg_nr;
    ver_t ver_id;
};

struct db_seg {
    ver_t ver_id;
    char data[SEGMENT_SIZE];
};

//    INIT_RADIX_TREE(my_tree, gfp_mask);
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




