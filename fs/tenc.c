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

struct page_decrypt_work {
	struct work_struct work;
	struct page *page;
	unsigned int offset;
	unsigned int len;
};

static int _tenc_should_encrypt(struct inode *inode) {
	struct dentry *dentry = d_find_any_alias(inode);
	if (!dentry) {
		printk(KERN_WARNING "_tenc_should_encrypt did not found an dentry for inode.\n");
		return 0;
	}

	printk_ratelimited(KERN_WARNING "generic_getxattr returned = %d\n", generic_getxattr(dentry, "user.encrypt", NULL, 0));
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
		printk(KERN_ERR "tenc_decrypt_buffer_head "
			   "bh->b_page->mapping == NULL\n");
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
EXPORT_SYMBOL(tenc_write_needs_page_switch);

/*
 * Encrypts the given buffer_head, but uses the dst_page as the destination
 * rather then the one indicated by buffer_head. The dst_page will be
 * the page reserved via page switch mechanism.
 */
void tenc_encrypt_block(struct buffer_head *bh, struct page *dst_page) {
	struct inode *inode = _tenc_safe_bh_to_inode(bh);
	struct page *src_page = bh->b_page;

	if (inode && _tenc_should_encrypt(inode)) {
		int i, pos;
		char *dst_addr = kmap(dst_page);
		char *src_addr = kmap(src_page);

		printk(KERN_INFO "encrypt block %d of length %d\n",
				(int)_tenc_page_pos_to_blknr(src_page, inode, bh_offset(bh)),
				(int)bh->b_size);
		for (i = 0, pos = bh_offset(bh); i < bh->b_size; i++, pos++) {
			dst_addr[pos] = ~src_addr[pos];
		}

		kunmap(src_page);
		kunmap(dst_page);
	}
}
EXPORT_SYMBOL(tenc_encrypt_block);

static void _tenc_decrypt_page_worker(struct page_decrypt_work *work) {
	int i, pos;
	char *addr;
	printk(KERN_INFO "decrypting page bl. %d of length %d\n",
			(int)_tenc_page_pos_to_blknr(page, inode, offset),
			(int)len);

//		spin_lock_irqsave(&atomic_kmap_lock, flags);
//		addr = kmap_atomic(page);
//		for (i = 0, pos = offset; i < len; i++, pos++) {
//			addr[pos] = ~addr[pos];
//		}
//		kunmap_atomic(addr);
//		spin_unlock_irqrestore(&atomic_kmap_lock, flags);

}

/*
 * Schedules decryption of the page if it's necessary.
 * Returns 1 to notify the page read code to leave the page locked and
 * not-Uptodate. Return 0 otherwise.
 */
int tenc_decrypt_page(struct page *page, unsigned int offset,
		unsigned int len) {
	struct inode *inode = page->mapping->host;

	if (_tenc_should_encrypt(inode)) {
		struct page_decrypt_work *work;
		work = kmalloc(sizeof(struct page_decrypt_work), GFP_ATOMIC);
		    if (work) {

		      INIT_WORK( (struct work_struct *)work, my_wq_function );

		      work->x = 1;

		      ret = queue_work( my_wq, (struct work_struct *)work );

		    }
		return 1;
	}

	return 0;
}
EXPORT_SYMBOL(tenc_decrypt_page);

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
EXPORT_SYMBOL(tenc_decrypt_buffer_head);
