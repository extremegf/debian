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

static int _tenc_should_encrypt(struct inode *inode) {
	struct dentry *dentry = d_find_any_alias(inode);
	if (!dentry) {
		printk(KERN_WARNING "_tenc_should_encrypt did not found an inode for inode.");
		return 0;
	}
	return 0 < generic_getxattr(dentry, "user.encrypt", NULL, 0);
}

static struct inode *_tenc_safe_bh_to_inode(struct buffer_head *bh) {
	struct inode *inode;

	if (!bh) {
		printk(KERN_ERR "tenc_decrypt_buffer_head got a NULL buffer_head.");
		return NULL;
	}

	if (!bh->b_assoc_map) {
		return NULL;  /* This is expected */
	}

	inode = bh->b_assoc_map->host;

	if (!inode) {
		printk(KERN_ERR "tenc_decrypt_buffer_head buffer_head had a NULL inode.");
	}

    return inode;
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

		kprint(KERN_INFO "encrypting block %d of length %d\n", (int)bh->b_blocknr, (int)bh->b_size);
		for (i = 0, pos = bh_offset(bh); i < bh->b_size; i++, pos++) {
			dst_addr[pos] = ~src_addr[pos];
		}

		kunmap(src_page);
		kunmap(dst_page);
	}
}
EXPORT_SYMBOL(tenc_encrypt_block);

/*
 * Decrypts a single file block. No checks.
 */
static void _tenc_decrypt_bh(struct buffer_head *bh) {
	struct page *page = bh->b_page;
	int i, pos;
	char *addr = kmap(page);

	kprint(KERN_INFO "decrypting block %d of length %d\n", (int)bh->b_blocknr, (int)bh->b_size);
	for (i = 0, pos = bh_offset(bh); i < bh->b_size; i++, pos++) {
		addr[pos] = ~addr[pos];
	}

	kunmap(page);
}

/*
 * Decrypts blocks associated with this page. The should be only one
 * anyway.
 */
void tenc_decrypt_full_page(struct page *page) {
	struct inode *inode = page->mapping->host;

	if (_tenc_should_encrypt(inode)) {
		struct buffer_head *bh, *head;
		head = bh = page_buffers(page);
		do {
			_tenc_decrypt_bh(bh);
		} while ((bh = bh->b_this_page) != head);
	}
}
EXPORT_SYMBOL(tenc_decrypt_full_page);

/*
 * Decrypts a single file block.
 */
void tenc_decrypt_buffer_head(struct buffer_head *bh) {
	struct inode *inode = _tenc_safe_bh_to_inode(bh);

	if (inode && _tenc_should_encrypt(inode)) {
		_tenc_decrypt_bh(bh);
	}
}
EXPORT_SYMBOL(tenc_decrypt_buffer_head);
