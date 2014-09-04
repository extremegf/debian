/*
 * Module handles the transactional operations on the database.
 *
 * Author: Przemyslaw Horban <p.horban@mimuw.edu.pl>
 */

#include <linux/list.h>
#include <linux/radix-tree.h>
#include <linux/semaphore.h>
#include <linux/rwsem.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/rcupdate.h>

#include "transaction.h"

#define RADIX_TREE_GANG_SIZE 100

typedef size_t ver_t;
typedef enum { RECURSIVE, NO_RECURSIVE } recursive_t;
typedef enum { REBASED, COLLISION } rebase_t;

struct seg_read {
    size_t seg_nr;
    ver_t ver_id;
    struct list_head other_reads;
};

struct db_seg {
    ver_t ver_id;
    char data[SEGMENT_SIZE];
};

struct db_version {
    struct radix_tree_root segments;
    struct db_version *parent;
    struct list_head all_other;
    size_t child_cnt;  // Not kept fresh at all times!
};

struct trans_context_t {
    ver_t ver_id;
    struct list_head reads;
    struct db_version *ver;
};

// DB versions path end. Readers obtain this via RCU. Writers use the spinlock.
static struct db_version *db_cur_ver;
static struct semaphore db_cur_ver_w_lock;
static size_t commits_since_compact;

// Represents infinite number of 0ed segments.
static struct db_seg null_seg;

// List of all db version. Used for compaction and cleanup.
static struct list_head all_db_vers;

// Next version id.
static ver_t next_ver;
static spinlock_t next_ver_lock;

// Locks chain traversal, during compaction.
struct rw_semaphore chain_rw_sem;

static struct db_version *new_db_version(struct db_version *parent)
{
    struct db_version *ver = kmalloc(sizeof(struct db_version), GFP_KERNEL);

    if (!ver)
        return NULL;

    INIT_RADIX_TREE(&ver->segments, GFP_KERNEL);
    list_add(&ver->all_other, &all_db_vers);
    ver->parent = parent;
    return ver;
}

static void destroy_db_version(struct db_version *ver)
{
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

int trans_init(void)
{
    INIT_LIST_HEAD(&all_db_vers);

    db_cur_ver = new_db_version(NULL);
    if (!db_cur_ver)
        return -ENOMEM;

    commits_since_compact = 0;

    memset(&null_seg.data, 0, SEGMENT_SIZE);
    null_seg.ver_id = 0;

    next_ver = 1;

    init_rwsem(&chain_rw_sem);

    sema_init(&db_cur_ver_w_lock, 1);
    spin_lock_init(&next_ver_lock);

    return 0;
}

void trans_destroy(void)
{
    struct db_version *ver, *n;
    list_for_each_entry_safe(ver, n, &all_db_vers, all_other) {
        destroy_db_version(ver);
    }
}

/*
 * Looks for a segment in db_version chain. If RECURSIVE set, it will
 * always succeed, falling back to the null_seg (all 0) if necessary.
 * If NO_RECURSIVE is set, it will only look for the segment in the
 * given db_version, returning null if it's not found.
 */
static struct db_seg* find_segment(struct db_version *ver, size_t seg_nr,
                                   recursive_t recurse)
{
    struct db_seg *found_db_seg = radix_tree_lookup(&ver->segments, seg_nr);
    if(found_db_seg) {
        return found_db_seg;
    } else {
        if(recurse == NO_RECURSIVE) {
            return NULL;
        } else if(ver->parent) {
            return find_segment(ver->parent, seg_nr, recurse);
        } else {
            // We reached the root. Above is an infinite 0ed space.
            return &null_seg;
        }
    }
}

/*
 * Performs copy-on-write of a db_version segment. Called when we know
 * that ver does not contain it. May fail due to lack of memory, in
 * which case it returns NULL.
 */
static struct db_seg* mimic_segment(struct db_version *ver, ver_t new_ver_id,
                                    size_t seg_nr)
{
    int err;
    struct db_seg *dst_seg = kmalloc(GFP_KERNEL, sizeof(struct db_seg));
    struct db_seg *src_seg;

    if(!dst_seg)
        return NULL;

    src_seg = find_segment(ver, seg_nr, RECURSIVE);

    dst_seg->ver_id = new_ver_id;
    memcpy(&dst_seg->data, &src_seg->data, SEGMENT_SIZE);

    err = radix_tree_insert(&ver->segments, seg_nr, dst_seg);
    printk(KERN_INFO "radix_tree_insert err=%d\n", err);

    if(err) {
        kfree(dst_seg);
        return NULL;
    }

    return dst_seg;
}

/*
 * Checks if the transaction can be rebased on the given db_version. This is
 * true if segments read from it's current parent match with the new parent.
 * The concept is, that if all reads look the same, the transaction would
 * produce the same results, so we can safely change it's parent db_version.
 */
static rebase_t trans_rebase(struct trans_context_t *trans,
                             struct db_version *onto)
{
    struct seg_read *seg_read;
    list_for_each_entry(seg_read, &trans->reads, other_reads) {
        struct db_seg *seg = radix_tree_lookup(&onto->segments,
                                               seg_read->seg_nr);

        if(seg_read->ver_id != seg->ver_id)  {
            return COLLISION;
        }
    }

    trans->ver->parent = onto;

    return REBASED;
}

/*
 * Fetches the segment with the given number. Saves info about reads
 * in the context for future rebases.
 * Returns NULL if kmalloc fails.
 */
char *get_read_segment(struct trans_context_t *trans, size_t seg_nr)
{
    struct db_seg *seg;
    down_read(&chain_rw_sem);
    seg = find_segment(trans->ver, seg_nr, NO_RECURSIVE);

    if (!seg) {
        struct seg_read *read = kmalloc(sizeof(struct seg_read), GFP_KERNEL);

        if (!read)
            return NULL;

        seg = find_segment(trans->ver, seg_nr, RECURSIVE);

        read->seg_nr = seg_nr;
        read->ver_id = seg->ver_id;

        list_add(&read->other_reads, &trans->reads);
    }

    // seg will not be kfree'ed while a read is taking place.
    up_read(&chain_rw_sem);

    return seg->data;
}


/*
 * Obtains a segment for writing in the current transactions db_segment.
 * Does a copy-on-write if necessary.
 */
char *get_write_segment(struct trans_context_t *trans, size_t seg_nr)
{
    // read semaphore is enough here. All we need to protect against are
    // writes done by chain compression.
    struct db_seg *seg;
    down_read(&chain_rw_sem);
    seg = find_segment(trans->ver, seg_nr, NO_RECURSIVE);

    printk(KERN_INFO "get_write_segment: seg = %p\n", seg);

    if(!seg) {
        seg = mimic_segment(trans->ver, trans->ver_id, seg_nr);
        printk(KERN_INFO "get_write_segment: mimic_segment seg=%p\n", seg);
    }

    // seg will not be kfree'ed while a write is taking place.
    up_read(&chain_rw_sem);

    return seg->data;
}

/*
 * Recomputes all child_cnt variables in all of the db_versions in existence.
 * Assumes chain_rw_sem in write lock.
 */
static void update_child_cnt(void)
{
    struct db_version *ver, *n;
    list_for_each_entry_safe(ver, n, &all_db_vers, all_other) {
        ver->child_cnt = 0;
    }

    list_for_each_entry_safe(ver, n, &all_db_vers, all_other) {
        if (ver->parent) {
            ver->parent->child_cnt += 1;
        }
    }
}

/*
 * Moves those db_segments from the parent into this db_version, which
 * do not have a newer-versioned counterpart. Then removes the parent.
 */
static int merge_with_parent(struct db_version *ver)
{
    struct db_version *parent = ver->parent;
    size_t indices[RADIX_TREE_GANG_SIZE];
    void *db_segs[RADIX_TREE_GANG_SIZE];
    struct db_seg *seg;
    size_t found, i, start = 0;

    // Now pick segments who have no newer counterpart.
    do {
        void **slot;
        struct radix_tree_iter iter;
        found = 0;

        radix_tree_for_each_slot(slot, &parent->segments, &iter, start)	{
            indices[found] = iter.index;
            db_segs[found] = *slot;
            found += 1;

            if (found == RADIX_TREE_GANG_SIZE) {
                break;
            }
        }

        start = iter.index;

        for (i = 0; i < found; i++) {
            seg = radix_tree_lookup(&ver->segments, indices[i]);
            if (!seg) {
                // No replacement found. We must add it to ver.
                int err;
                err = radix_tree_insert(&ver->segments, indices[i], db_segs[i]);
                if (err) {
                    // We ran out of memory. But we made sure that the
                    // compaction can be safely aborted at any point.
                    // The db will simply be a little slower.
                    return err;
                }

            }
            radix_tree_delete(&parent->segments, indices[i]);
        }
    } while(found > 0);

    // The parent was emptied. We can safely remove it from the chain.
    ver->parent = parent->parent;
    parent->parent = NULL;
    destroy_db_version(parent);

    return 0;
}

/*
 * Shortens the db_version chain by merging links that have only one child
 * into the child.
 */
static void optimize_chain(void)
{
    struct db_version *ver;
    down_write(&chain_rw_sem);
    commits_since_compact = 0;

    update_child_cnt();

    rcu_read_lock();
    ver = rcu_dereference(db_cur_ver);
    rcu_read_unlock();

    while(ver && ver->parent) {
        if (ver->parent->child_cnt == 1) {
            // Merge parent into ver.
            int err;
            err = merge_with_parent(ver);
            if (err) {
                // We ran out of memory. Abort the optimization.
                ver = NULL;
            }
        } else {
            // Step.
            ver = ver->parent;
        }
    }

    up_write(&chain_rw_sem);
}


/*
 * Tries to rebase the commit and move the head to it. If a race happens
 * and head changes, it will return RETRY_COMMIT. If rebase fails, it will
 * return ROLLBACK.
 */
static trans_result_t do_commit(struct trans_context_t *trans,
                                int *compaction_necessary)
{
    struct db_version *cur_ver;
    rebase_t rebase;

    down(&db_cur_ver_w_lock);

    // Is this necessary when I don't use rcu_synchronize?
    // I'll just use it everywhere.
    rcu_read_lock();
    cur_ver = rcu_dereference(db_cur_ver);
    rcu_read_unlock();

    rebase = trans_rebase(trans, cur_ver);

    if (rebase == COLLISION) {
        up(&db_cur_ver_w_lock);
        return ROLLBACK;
    } else { /* rebase == REBASED */
        // chain_rw_sem protects against compaction code and
        // db_cur_ver_w_lock protects against other writers.
        commits_since_compact++;

        *compaction_necessary =
            (commits_since_compact > COMMITS_BEFORE_COMAPCTION);

        rcu_assign_pointer(db_cur_ver, trans->ver);
        up(&db_cur_ver_w_lock);
        return COMMIT;
    }
}

/*
 * Destroys the transaction object. If result is ROLLBACK, then
 * commit will simply be lost. If it is COMMIT, then db will try to
 * commit the change. This might still fail, producing a ROLLBACK.
 * If the change is saved, COMMIT will be returned.
 */
trans_result_t finish_transaction(trans_result_t result,
                                  struct trans_context_t *trans)
{
    struct seg_read *seg, *n;
    int compaction_necessary = 0;
    down_read(&chain_rw_sem);

    // Try to commit.
    if (result == COMMIT) {
        result = do_commit(trans, &compaction_necessary);
    }

    // Destroy the db_version only if this is a requested or forced ROLLBACK.
    if (result == ROLLBACK) {
        destroy_db_version(trans->ver);
    }

    // Always clear out the reads.
    list_for_each_entry_safe(seg, n, &trans->reads, other_reads) {
        list_del(&seg->other_reads);
        kfree(seg);
    }

    kfree(trans);

    up_read(&chain_rw_sem);

    if (compaction_necessary) {
        optimize_chain();
    }

    return result;
}

/*
 * Creates a new transaction context. Must be freed with finish_transaction.
 */
struct trans_context_t *new_trans_context(void)
{
    unsigned long flags;
    struct trans_context_t *trans =
        kmalloc(sizeof(struct trans_context_t), GFP_KERNEL);

    if (!trans)
        return NULL;

    rcu_read_lock();
    trans->ver = new_db_version(rcu_dereference(db_cur_ver));
    rcu_read_unlock();
    if (!trans->ver) {
        kfree(trans);
        return NULL;
    }

    spin_lock_irqsave(&next_ver_lock, flags);
    trans->ver_id = next_ver;
    next_ver += 1;
    spin_unlock_irqrestore(&next_ver_lock, flags);

    INIT_LIST_HEAD(&trans->reads);

    return trans;
}

static void printk_db_version(const char *entry_prefix, const char *pre_indent,
                             const char *indent, struct db_version *ver)
{
    void **slot;
    struct radix_tree_iter iter;
    size_t i;

    printk(KERN_INFO "%s%sdb_version:\n", pre_indent, entry_prefix);
    printk(KERN_INFO "%s%ssegments:\n", pre_indent, indent);
    radix_tree_for_each_slot(slot, &ver->segments, &iter, 0)	{
        struct db_seg *seg = *slot;
        printk(KERN_INFO "%s%s%s%3d: %3d [ ", pre_indent, indent, indent,
               (int)iter.index, (int)seg->ver_id);
        for (i = 0; i < SEGMENT_SIZE; i++) {
            printk(KERN_INFO "%2X ", (int)seg->data[i]);
        }
        printk(KERN_INFO "]\n");
    }
    printk(KERN_INFO "%s%schild_cnt: %d\n", pre_indent, indent,
           (int)ver->child_cnt);
}

/*
 * Logs a readable representation of the database.
 */
void printk_db_versions(void)
{
    struct db_version *ver = db_cur_ver;
    while(ver) {
    	printk_db_version("^", "", "    ", ver);
    	ver = ver->parent;
    }
}

