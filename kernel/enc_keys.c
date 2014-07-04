/*
 * Function for handling task encryption keys.
 *
 * Author: Przemyslaw Horban (p.horban@mimuw.edu.pl)
 */

#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <asm/current.h>

void enc_keys_task_init(struct task_struct *tsk) {
	INIT_LIST_HEAD(&tsk->enc_keys);
}

void exit_task_enc_keys(struct task_struct *tsk) {
	struct list_head *pos, *store;

	list_for_each_safe(pos, store, &tsk->enc_keys) {
		struct task_enc_key *key;
		key = list_entry(pos, struct task_enc_key, other_keys);

		list_del(&key->other_keys);
		kfree(key);
	}
}

int copy_enc_keys(unsigned long clone_flags, struct task_struct *tsk) {
	struct list_head *pos;
	list_for_each(pos, &current->enc_keys) {
		struct task_enc_key *key, *key_copy;

		key_copy = kmalloc(sizeof(struct task_enc_key), GFP_KERNEL);
		if (!key_copy) {
			exit_task_enc_keys(tsk);
			/* We must return here. Iterator is destroyed */
			return -ENOMEM;
		}

		key = list_entry(pos, struct task_enc_key, other_keys);
		memcpy(key_copy->key_bytes, key->key_bytes, sizeof(key->key_bytes));
		memcpy(key_copy->key_id, key->key_id, sizeof(key->key_id));

		list_add(&key_copy->other_keys, &tsk->enc_keys);
	}
	return 0;
}
