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
#include <linux/list.h>
#include <linux/scatterlist.h>
#include <crypto/aes.h>
#include <asm/current.h>

#define KEY_ID_XATTR "user.enc_key_id"
#define MD5_LENGTH 16
#define IV_XATTR "user.enc_iv"
#define KEY_LENGTH 16

union counter_bytes {
	unsigned long counter;
	char bytes[AES_BLOCK_SIZE];
};

struct inode_key {
	struct inode *inode;
	uint8_t key_bytes[KEY_LENGTH];
	uint8_t key_id[MD5_LENGTH];

	struct list_head other;
};

LIST_HEAD(inode_keys);
spinlock_t inode_keys_lock;

static struct inode_key *_tenc_find_inode_key(struct inode *inode) {
	struct inode_key *ikey;
	struct list_head *pos;
	list_for_each(pos, &inode_keys) {
		ikey = list_entry(pos, struct inode_key, other);
		if (ikey->inode == inode)
			return ikey;
	}
	return NULL;
}

static struct inode_key *_tenc_add_inode_key(struct inode *inode,
		struct task_enc_key *task_key) {
	struct inode_key *ikey = kmalloc(sizeof(struct inode_key), GFP_KERNEL);
	if (!ikey)
		return ikey;
	memcpy(ikey->key_bytes, task_key->key_bytes, KEY_LENGTH);
	memcpy(ikey->key_id, task_key->key_id, MD5_LENGTH);
	ikey->inode = inode;
	list_add(&ikey->other, &inode_keys);
	return ikey;
}

static void _tenc_del_inode_key(struct inode_key *ikey) {
	list_del(&ikey->other);
	kfree(ikey);
}

/* Note that the key search and adding are not synchronized. This is
 * intentional. User application shoud
 */
asmlinkage int sys_addkey(unsigned char __user *user_key) {
	struct task_enc_key *tsk_key;
	unsigned long flags;
    struct scatterlist sg;
    struct crypto_hash *tfm;
    struct hash_desc desc;
    int i;

	tsk_key = kmalloc(sizeof(struct task_enc_key), GFP_KERNEL);

	if (!tsk_key)
		return -ENOMEM;

	if (copy_from_user(&tsk_key->key_bytes, user_key,
			sizeof(tsk_key->key_bytes))) {
		kfree(tsk_key);
		printk(KERN_INFO "sys_addkey: copy_from_user failed\n");
		return -EFAULT;
	}

    tfm = crypto_alloc_hash("md5", 0, CRYPTO_ALG_ASYNC);

    if (IS_ERR(tfm)) {
		printk(KERN_INFO "sys_addkey: crypto_alloc_hash failed\n");
    	kfree(tsk_key);
		return -EFAULT;
    }

    desc.tfm = tfm;
    desc.flags = 0;

    sg_init_one(&sg, tsk_key->key_bytes, sizeof(tsk_key->key_bytes));
    if (crypto_hash_init(&desc) ||
    		crypto_hash_update(&desc, &sg, sizeof(tsk_key->key_bytes)) ||
    		crypto_hash_final(&desc, tsk_key->key_id)) {
		printk(KERN_INFO "sys_addkey: hashing failed\n");
    	crypto_free_hash(tfm);
    	kfree(tsk_key);
    	return -EFAULT;
    }

    printk(KERN_INFO "New secret key added to current, key_id=\"");
    for (i = 0; i < 16; i++) {
        printk("\\x%02x", tsk_key->key_id[i]);
    }
    printk("\"\n");

	spin_lock_irqsave(&current->enc_keys_lock, flags);
	list_add(&tsk_key->other_keys, &current->enc_keys);
	spin_unlock_irqrestore(&current->enc_keys_lock, flags);
	return 0;
}
EXPORT_SYMBOL(sys_addkey);

struct page_decrypt_work {
	struct work_struct work;
	struct page *page;
};

static int _tenc_encrypted_file(struct inode *inode) {
	unsigned long flags;
	struct inode_key *ikey;
	spin_lock_irqsave(&inode_keys_lock, flags);
	ikey = _tenc_find_inode_key(inode);
	spin_unlock_irqrestore(&inode_keys_lock, flags);
	return ikey != NULL;
}

static void printk_key_id(char *key_id) {
	int i;
	for (i=0; i<16; i++) {
		printk("%02x", (int)key_id[i]);
	}
}

/* Does not grant ownership of the pointer. */
static struct task_enc_key *_tenc_find_task_key(unsigned char key_id[MD5_LENGTH]) {
	struct list_head *pos;
	unsigned long flags;
	struct task_enc_key *key;

	BUG_ON(sizeof(key->key_id) != MD5_LENGTH);

	spin_lock_irqsave(&current->enc_keys_lock, flags);
	list_for_each(pos, &current->enc_keys) {
		key = list_entry(pos, struct task_enc_key, other_keys);
		if (memcmp(key->key_id, key_id, MD5_LENGTH) == 0) {
			spin_unlock_irqrestore(&current->enc_keys_lock, flags);
			return key;
		}
	}
	spin_unlock_irqrestore(&current->enc_keys_lock, flags);
	return NULL;
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
	if (inode && _tenc_encrypted_file(inode)) {
		return 1;
	}
	return 0;
}
EXPORT_SYMBOL(tenc_write_needs_page_switch);

/*
 * Returns true if file is encrypted
 */
int tenc_file_is_encrypted(struct inode *inode) {
	if (inode && _tenc_encrypted_file(inode)) {
		return 1;
	}
	return 0;
}
EXPORT_SYMBOL(tenc_file_is_encrypted);

static void _tenc_aes128_ctr_page(struct inode *inode, struct page *page) {
	struct dentry *dentry = d_find_any_alias(inode);
    int i, pos, j;
    unsigned char key_id[MD5_LENGTH];
	unsigned char key_bytes[KEY_LENGTH];
	struct crypto_cipher *cipher;
    char iv[AES_BLOCK_SIZE], enc_block[AES_BLOCK_SIZE];
    union counter_bytes cb;
    char *addr;
	unsigned long flags;
	struct inode_key *ikey;


	if (!dentry) {
		printk(KERN_ERR "_tenc_aes128_ctr_page encryption failure. "
				"No file dentry\n");
		return;
	}

	if (MD5_LENGTH != generic_getxattr(dentry, KEY_ID_XATTR, key_id,
			MD5_LENGTH)) {
		dput(dentry);
		printk(KERN_ERR "_tenc_aes128_ctr_page encryption failure. "
				"No key_id xattr\n");
		return;
	}

	if (AES_BLOCK_SIZE != generic_getxattr(dentry, IV_XATTR, iv,
			AES_BLOCK_SIZE)) {
		dput(dentry);
		printk(KERN_ERR "_tenc_aes128_ctr_page encryption failure. "
				"No IV xattr\n");
		return;
	}

	dput(dentry);

	spin_lock_irqsave(&inode_keys_lock, flags);
	ikey = _tenc_find_inode_key(inode);
	if (ikey) {
		memcpy(key_bytes, ikey->key_bytes, KEY_LENGTH);
	}
	spin_unlock_irqrestore(&inode_keys_lock, flags);

	if (!ikey) {
		printk(KERN_ERR "_tenc_aes128_ctr_page encryption failure. "
				"Inode did not have the enc_key\n");
		return;
	}

	cipher = crypto_alloc_cipher("aes", 0, 0);

	if (IS_ERR(cipher)) {
		printk(KERN_ERR "_tenc_aes128_ctr_page encryption failure. "
				"Could not allocate cipher. err=%ld\n", PTR_ERR(cipher));
		return;
	}

	if (crypto_cipher_setkey(cipher, key_bytes, KEY_LENGTH)) {
		printk(KERN_ERR "_tenc_aes128_ctr_page encryption failure. "
				"Failed to set key crypto_cipher_setkey. err=%ld\n",
				PTR_ERR(cipher));
		crypto_free_cipher(cipher);
		return;
	}

	// Create the (IV_pliku ^ (indeks_bloku * rozmiar_bloku)) Nonce
	// This system does not break if the last file block isn't actually full.
	memset(cb.bytes, 0, sizeof(union counter_bytes));
	cb.counter = _tenc_page_pos_to_blknr(page, inode, 0) * PAGE_SIZE;
	for (j = 0; j < AES_BLOCK_SIZE; j++) {
		iv[j] ^= cb.bytes[j];
	}

	addr = kmap(page);

	for (i = 0, pos = 0; i < PAGE_SIZE / AES_BLOCK_SIZE;
			i++, pos += AES_BLOCK_SIZE) {
		memset(cb.bytes, 0, sizeof(union counter_bytes));
		cb.counter = i;
		memcpy(enc_block, iv, AES_BLOCK_SIZE);

		for (j = 0; j < AES_BLOCK_SIZE; j++) {
			enc_block[j] ^= cb.bytes[j];
		}

		crypto_cipher_encrypt_one(cipher, enc_block, enc_block);

		for (j = 0; j < AES_BLOCK_SIZE; j++) {
			addr[pos + j] ^= enc_block[j];
		}
	}

	kunmap(page);
	crypto_free_cipher(cipher);
}

/*
 * Encrypts the given buffer_head, but uses the dst_page as the destination
 * rather then the one indicated by buffer_head. The dst_page will be
 * the page reserved via page switch mechanism.
 */
void tenc_encrypt_block(struct buffer_head *bh, struct page *dst_page) {
	struct inode *inode = _tenc_safe_bh_to_inode(bh);
	struct page *src_page = bh->b_page;

	if (inode && _tenc_encrypted_file(inode)) {
		char *dst_addr = kmap(dst_page);
		char *src_addr = kmap(src_page);

		printk(KERN_INFO "encrypt block %d\n",
				(int)_tenc_page_pos_to_blknr(src_page, inode, 0));

		memcpy(dst_addr, src_addr, PAGE_SIZE);
		dst_page->index = src_page->index;
		_tenc_aes128_ctr_page(inode, dst_page);

		kunmap(src_page);
		kunmap(dst_page);
	}
}
EXPORT_SYMBOL(tenc_encrypt_block);

static void _tenc_decrypt_page_worker(struct work_struct *_work) {
	struct page_decrypt_work *work = (struct page_decrypt_work*)_work;
	struct page* page = work->page;
	struct inode *inode = page->mapping->host;

	printk(KERN_INFO "decrypting page bl. %d\n",
			(int)_tenc_page_pos_to_blknr(page, inode, 0));

	_tenc_aes128_ctr_page(inode, page);

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

	if (_tenc_encrypted_file(inode)) {
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
EXPORT_SYMBOL(tenc_decrypt_page);

/*
 * Decrypts a single file block. Special case for pages containing
 * non-continous series of blocks (end of file does not count).
 */
void tenc_decrypt_buffer_head(struct buffer_head *bh) {
	struct inode *inode = _tenc_safe_bh_to_inode(bh);

	if (inode && _tenc_encrypted_file(inode)) {
		/* Unimplemented.
		 *
		 * This is not necessary on machines where disk block size == PAGE_SIZE.
		 * The virtual machine we use has this property.
		 */
		printk(KERN_ERR "tenc_decrypt_buffer_head: decryption of "
				"partial page requested\n");
		BUG();
	}
}
EXPORT_SYMBOL(tenc_decrypt_buffer_head);

/*
 * Checks if the caller can open given file. Returns 1 if he can, 0 otherwise.
 */
int tenc_can_open(struct inode *inode, struct file *filp) {
	char user_key_id[MD5_LENGTH];
	int atr_len = generic_getxattr(filp->f_dentry, KEY_ID_XATTR,
			user_key_id, MD5_LENGTH);
	struct inode_key *ikey;
	unsigned long flags;

	spin_lock_irqsave(&inode_keys_lock, flags);
	ikey = _tenc_find_inode_key(inode);

    if (atr_len <= 0 && ikey) {
		printk(KERN_INFO "Inode has no key_id xattr but has an attached key."
				"Detaching the key.\n");
		_tenc_del_inode_key(ikey);
    	spin_unlock_irqrestore(&inode_keys_lock, flags);
    	return 1;
    }

    if (atr_len <= 0) {
    	spin_unlock_irqrestore(&inode_keys_lock, flags);
    	return 1;
    }

    if (atr_len == MD5_LENGTH) {
    	struct task_enc_key *enc_key = _tenc_find_task_key(user_key_id);
    	if (enc_key && ikey) {
    		if (memcmp(enc_key->key_bytes, ikey->key_bytes, KEY_LENGTH) == 0) {
        		printk(KERN_INFO "Key found. Was already attached. "
        				"Allowing to open the file.\n");
            	spin_unlock_irqrestore(&inode_keys_lock, flags);
        		return 1;
    		}
    		else {
    			BUG(); // Likely a bug or security violation.
    		}
    	}
    	else if (!enc_key && ikey) {
    		// You do not have the key. Access denied.
        	spin_unlock_irqrestore(&inode_keys_lock, flags);
    		return 0;
    	}
    	else if (enc_key && !ikey) {
    		printk(KERN_INFO "Inode has no enc_key, but process has it. "
    				"Attaching key to inode.\n");
    		if (!_tenc_add_inode_key(inode, enc_key)) {
    			printk(KERN_WARNING "Not enough memory to open encrypted "
    					"file.\n");
    			spin_unlock_irqrestore(&inode_keys_lock, flags);
    			return 0; // No memory... The error is not very verbose.
    		}
        	spin_unlock_irqrestore(&inode_keys_lock, flags);
        	return 1;
    	}
    	else {
    		printk(KERN_INFO "Process did not have the key to open file.\n");
        	spin_unlock_irqrestore(&inode_keys_lock, flags);
        	return 0;
    	}
    }
	spin_unlock_irqrestore(&inode_keys_lock, flags);
	return 0;
}
EXPORT_SYMBOL(tenc_can_open);

void tenc_release(struct inode *inode, struct file *filp) {
	unsigned long flags;
	struct inode_key *ikey;
	spin_lock_irqsave(&inode_keys_lock, flags);
	ikey = _tenc_find_inode_key(inode);
	if (ikey) {
		_tenc_del_inode_key(ikey);
	}
	spin_unlock_irqrestore(&inode_keys_lock, flags);
}
EXPORT_SYMBOL(tenc_release);

long tenc_encrypt_ioctl(struct file *filp, unsigned char key_id[MD5_LENGTH]) {
	struct inode *inode;
	unsigned long iflags, flags;
    // TODO: No point making it really random.
	// This destroys security but changing it will make testing more difficult.
	// I'm leaving it as is for my final submit.
	char enc_iv[] = "1234567890123456"; // Lenght is AES_BLOCK_SIZE
	int err;
	struct task_enc_key *enc_key;
	struct inode_key *ikey;

	/* TODO: To make this really secure, we probably need some more locks and
	 * we would need to use system rather then user xattr prefix. I will more
	 * or less ignore these security issues though. There is no way I can get it
	 * right without community input and security done almost-right is
	 * worth nothing anyway.
	 */

	if (0 < generic_getxattr(filp->f_dentry, KEY_ID_XATTR, NULL, 0)) {
		printk(KERN_INFO "tenc_encrypt_ioctl: File is already encrypted\n");
		return -EEXIST;
	}

	err = generic_setxattr(filp->f_dentry, KEY_ID_XATTR, key_id, MD5_LENGTH, 0);
	if (err) {
		printk(KERN_INFO "tenc_encrypt_ioctl: Encrypted file generic_setxattr "
				"returned %d\n", err);
		return err;
	}

	err = generic_setxattr(filp->f_dentry, IV_XATTR, enc_iv, AES_BLOCK_SIZE, 0);
	if (err) {
		printk(KERN_INFO "tenc_encrypt_ioctl: Encrypted file generic_setxattr "
				"returned %d\n", err);
		return err;
	}

	inode = filp->f_inode;

	spin_lock_irqsave(&inode_keys_lock, flags);

	enc_key = _tenc_find_task_key(key_id);
	if (!enc_key) {
		printk(KERN_INFO "tenc_encrypt_ioctl: Caller does not have the "
				"requested key\n");
		spin_unlock_irqrestore(&inode_keys_lock, flags);
		generic_removexattr(filp->f_dentry, KEY_ID_XATTR);
		generic_removexattr(filp->f_dentry, IV_XATTR);
		return -EPERM;
	}
	else {
		ikey = _tenc_find_inode_key(inode);
		if (!ikey) {
    		if (!(ikey = _tenc_add_inode_key(inode, enc_key))) {
    			printk(KERN_WARNING "Not enough memory to open encrypted "
    					"file.\n");
    			spin_unlock_irqrestore(&inode_keys_lock, flags);
    			generic_removexattr(filp->f_dentry, KEY_ID_XATTR);
    			generic_removexattr(filp->f_dentry, IV_XATTR);
    			return -ENOMEM;
    		}
		}
	}

	spin_lock_irqsave(&inode->i_lock, iflags);

	/* This ensures that file can't be opened by anyone else when encryption
	 * if requested. This is more to avoid API misuse than for security. */
	if (atomic_read(&inode->i_count) > 0 ||
			atomic_read(&inode->i_dio_count) > 0 ||
			atomic_read(&inode->i_writecount) > 0) {
		printk(KERN_INFO "tenc_encrypt_ioctl: Encrypted file access denied - "
				"file opened more than once.\n");
		spin_unlock_irqrestore(&inode->i_lock, iflags);
		_tenc_del_inode_key(ikey);
		spin_unlock_irqrestore(&inode_keys_lock, flags);
		generic_removexattr(filp->f_dentry, KEY_ID_XATTR);
		generic_removexattr(filp->f_dentry, IV_XATTR);
		return -EINVAL;
	}

	if (inode->i_bytes > 0 || inode->i_blocks > 0) {
		printk(KERN_INFO "tenc_encrypt_ioctl: Encrypted file access denied - "
				"file not empty\n");
		spin_unlock_irqrestore(&inode->i_lock, iflags);
		_tenc_del_inode_key(ikey);
		spin_unlock_irqrestore(&inode_keys_lock, flags);
		generic_removexattr(filp->f_dentry, KEY_ID_XATTR);
		generic_removexattr(filp->f_dentry, IV_XATTR);
		return -EINVAL;
	}

	spin_unlock_irqrestore(&inode_keys_lock, flags);
	spin_unlock_irqrestore(&inode->i_lock, iflags);
	return 0;
}
EXPORT_SYMBOL(tenc_encrypt_ioctl);
