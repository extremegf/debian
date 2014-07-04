/*
 * Heart of transparent encryption system for ext4.
 *
 * Author: Przemyslaw Horban (p.horban@mimuw.edu.pl)
 */

#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/mm_types.h>
#include <linux/buffer_head.h>
#include <linux/fs.h>
#include <linux/xattr.h>
#include <linux/printk.h>
#include <linux/ratelimit.h>
#include <linux/workqueue.h>
#include <linux/linkage.h>
#include <linux/crypto.h>
#include <linux/spinlock.h>
#include <linux/scatterlist.h>
#include <asm/current.h>

#define KEY_ID_XATTR "user.enc_key_id"
#define MD5_LENGTH 16

/* Note that the key search and adding are not synchronized. This is
 * intentional. User application shoud
 */
asmlinkage int sys_addkey(unsigned char *user_key) {
	struct task_enc_key *tsk_key;
	unsigned long flags;
    struct scatterlist sg;
    struct crypto_hash *tfm;
    struct hash_desc desc;

	tsk_key = kmalloc(sizeof(struct task_enc_key), GFP_KERNEL);

	if (!tsk_key)
		return -ENOMEM;

	if (!copy_from_user(&tsk_key->key_bytes, user_key,
			sizeof(tsk_key->key_bytes))) {
		kfree(tsk_key);
		return -EFAULT;
	}

    tfm = crypto_alloc_hash("md5", 0, CRYPTO_ALG_ASYNC);

    if (IS_ERR(tfm)) {
		kfree(tsk_key);
		return -EFAULT;
    }

    desc.tfm = tfm;
    desc.flags = 0;

    sg_init_one(&sg, tsk_key->key_bytes, sizeof(tsk_key->key_bytes));
    if (!crypto_hash_init(&desc) ||
    		!crypto_hash_update(&desc, &sg, sizeof(tsk_key->key_bytes)) ||
    		!crypto_hash_final(&desc, tsk_key->key_id)) {
    	crypto_free_hash(tfm);
    	kfree(tsk_key);
    	return -EFAULT;
    }

//    for (i = 0; i < 16; i++) {
//        printk(KERN_ERR "%d-%d\n", tsk_key->key_bytes[i], i);
//    }

	spin_lock_irqsave(&current->enc_keys_lock, flags);
	list_add(&tsk_key->other_keys, &current->enc_keys);
	spin_unlock_irqrestore(&current->enc_keys_lock, flags);
	return 0;
}


struct page_decrypt_work {
	struct work_struct work;
	struct page *page;
};

static int _tenc_should_encrypt(struct inode *inode) {
	struct dentry *dentry = d_find_any_alias(inode);
	if (!dentry) {
		// printk(KERN_WARNING
		// 	 	  "_tenc_should_encrypt did not found an dentry for inode.\n");
		return 0;
	}

	// mpage_end_io + 0x6b
	// printk_ratelimited(KERN_WARNING "generic_getxattr returned = %d\n",
	//                    generic_getxattr(dentry, "user.encrypt", NULL, 0));
	return 0 < generic_getxattr(dentry, "user.encrypt", NULL, 0);
}

static struct inode *_tenc_safe_bh_to_inode(struct buffer_head *bh) {
	struct inode *inode;

	if (!bh) {
		printk(KERN_ERR "tenc_decrypt_buffer_head got a NULL buffer_head.\n");
		return NULL;
	}

	if (!bh->b_page) {
		printk(KERN_ERR "tenc_decrypt_buffer_head got a bh->b_page == NULL\n");
		return NULL;
	}


	if (!bh->b_page->mapping) {
		// printk(KERN_ERR "tenc_decrypt_buffer_head "
		// 	   "bh->b_page->mapping == NULL\n");
		return NULL;
	}

	inode = bh->b_page->mapping->host;

	if (!inode) {
		printk(KERN_ERR "tenc_decrypt_buffer_head "
				        "page->mapping had a NULL host (inode).\n");
	}

    return inode;
}

static sector_t _tenc_page_pos_to_blknr(struct page *page, struct inode *inode,
										unsigned int offset) {
	sector_t start_blk_nr = (sector_t)page->index <<
							(PAGE_CACHE_SHIFT - inode->i_blkbits);
	return start_blk_nr + (offset / (1 << inode->i_blkbits));
}

/*
 * Returns true if we intend to encrypt the given buffer_head.
 * This means that write code must allocate a separate page to isolate
 * our encryption from mmaps etc.
 */
int tenc_write_needs_page_switch(struct buffer_head *bh) {
	struct inode *inode = _tenc_safe_bh_to_inode(bh);

	if (inode && _tenc_should_encrypt(inode)) {
		return 1;
	}
	return 0;
}

/*
 * Encrypts the given buffer_head, but uses the dst_page as the destination
 * rather then the one indicated by buffer_head. The dst_page will be
 * the page reserved via page switch mechanism.
 */
void tenc_encrypt_block(struct buffer_head *bh, struct page *dst_page) {
	struct inode *inode = _tenc_safe_bh_to_inode(bh);
	struct page *src_page = bh->b_page;

	if (inode && _tenc_should_encrypt(inode)) {
		int pos;
		char *dst_addr = kmap(dst_page);
		char *src_addr = kmap(src_page);

		printk(KERN_INFO "encrypt block %d\n",
				(int)_tenc_page_pos_to_blknr(src_page, inode, 0));

		for (pos = 0; pos < PAGE_SIZE; pos++) {
			dst_addr[pos] = ~src_addr[pos];
		}

		kunmap(src_page);
		kunmap(dst_page);
	}
}

static void _tenc_decrypt_page_worker(struct work_struct *_work) {
	struct page_decrypt_work *work = (struct page_decrypt_work*)_work;
	int pos;
	char *addr;
	struct page* page = work->page;
	struct inode *inode = page->mapping->host;

	printk(KERN_INFO "decrypting page bl. %d\n",
			(int)_tenc_page_pos_to_blknr(page, inode, 0));

	addr = kmap(page);
	for (pos = 0; pos < PAGE_SIZE; pos++) {
		addr[pos] = ~addr[pos];
	}
	kunmap(page);

	SetPageUptodate(page);
	unlock_page(page);
	kfree(work);
}

/*
 * Schedules decryption of the page if it's necessary.
 * Returns TENC_LEAVE_LOCKED to notify the page read code to leave the page
 * locked and not-Uptodate. Returns TENC_DECR_FAIL if it failed to schedule
 * the work. Return TENC_CAN_UNLOCK if the page can be immediately unlocked.
 */
int tenc_decrypt_page(struct page *page) {
	struct inode *inode = page->mapping->host;

	if (_tenc_should_encrypt(inode)) {
		struct page_decrypt_work *work;
		work = kmalloc(sizeof(struct page_decrypt_work), GFP_ATOMIC);
		if (work) {
			int err;
			INIT_WORK((struct work_struct *)work, _tenc_decrypt_page_worker);

            printk(KERN_INFO "Adding decryption work page=%p\n", page);
			work->page = page;

			err = schedule_work((struct work_struct *)work);

			BUG_ON(!err);  /* Fails if the same job is already scheduled */
			return TENC_LEAVE_LOCKED;
		}
		else {
			return TENC_DECR_FAIL;
		}
	}

	return TENC_CAN_UNLOCK;
}

/*
 * Decrypts a single file block. Special case for pages containing
 * non-continous series of blocks (end of file does not count).
 */
void tenc_decrypt_buffer_head(struct buffer_head *bh) {
	struct inode *inode = _tenc_safe_bh_to_inode(bh);

	if (inode && _tenc_should_encrypt(inode)) {
		/* Unimplemented.
		 *
		 * This is not necessary on machines where disk block size == PAGE_SIZE.
		 * The virtual machine we use has this property.
		 */
		BUG();
	}
}

/*
 * Checks if the caller can open given file. Returns 1 if he can, 0 otherwise.
 */
int tenc_can_open(struct inode *inode, struct file *filp) {
	// check for encryption xattrs
	// confirm process has the given key
	// TODO
	return 1;
}

long tenc_encrypt_ioctl(struct file *filp, unsigned long arg) {
	struct inode *inode;
	unsigned long iflags;
	int err;

	/* To make this really secure, we would need to add some locking
	 * and also use system. rather then user. xattr. I will more or less
	 * ignore the security issues though. There is no way I can get it
	 * right anyway and security done almost-right is worth nothing.
	 */

	if (0 < generic_getxattr(filp->f_dentry, KEY_ID_XATTR, NULL, 0)) {
		return -EEXIST;
	}

	err = generic_setxattr(filp->f_dentry, KEY_ID_XATTR, "1234", 4, 0); // TODO
	if (err) {
		return err;
	}

	inode = filp->f_inode;
	spin_lock_irqsave(&inode->i_lock, iflags);

	/* This ensures that file can't be opened by anyone else when encryption
	 * if requested. */
	if (atomic_read(&inode->i_count) > 1) {
		printk(KERN_INFO "Encrypted file access denied - file "
				"opened more than once.\n");
		spin_unlock_irqrestore(&inode->i_lock, iflags);
		generic_removexattr(filp->f_dentry, KEY_ID_XATTR);
		return -EACCES;
	}

	if (inode->i_bytes > 0) {
		printk(KERN_INFO "Encrypted file access denied - file not empty\n");
		spin_unlock_irqrestore(&inode->i_lock, iflags);
		generic_removexattr(filp->f_dentry, KEY_ID_XATTR);
		return -EACCES;
	}

	spin_unlock_irqrestore(&inode->i_lock, iflags);
	return 0;
}
