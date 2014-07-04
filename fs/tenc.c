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

static int _tenc_shold_encrypt(struct inode *inode) {

	/* xattr get type:
	 * int (*get)(struct dentry *dentry, const char *name, void *buffer,
	 *            size_t size, int handler_flags)
	 */
	struct dentry *dentry = d_find_any_alias(inode);
	if (!dentry) {
		return 0;
	}
	return 0 < (*(inode->i_sb->s_xattr))->get(dentry, "encrypt", NULL, 0, 0);
}


/*
 * Returns true if we intend to encrypt the given buffer_head.
 * This means that write code must allocate a separate page to isolate
 * our encryption from mmaps etc.
 */
int tenc_write_needs_page_switch(struct buffer_head *bh) {
	struct inode *inode = bh->b_assoc_map->host;

	if (_tenc_shold_encrypt(inode)) {
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
	struct inode *inode = bh->b_assoc_map->host;
	struct page *src_page = bh->b_page;

	if (_tenc_shold_encrypt(inode)) {
		int i, pos;
		char *dst_addr = kmap(dst_page);
		char *src_addr = kmap(src_page);

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

	if (_tenc_shold_encrypt(inode)) {
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
	struct inode *inode = bh->b_assoc_map->host;

	if (_tenc_shold_encrypt(inode)) {
		_tenc_decrypt_bh(bh);
	}
}
EXPORT_SYMBOL(tenc_decrypt_buffer_head);
