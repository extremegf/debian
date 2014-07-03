/*
 * Heart of transparent encryption system for ext4.
 *
 * Author: Przemyslaw Horban (p.horban@mimuw.edu.pl)
 */

/*
 * Returns true if we intend to encrypt the given buffer_head.
 * This means that write code must allocate a separate page to isolate
 * our encryption from mmaps etc.
 */
int tenc_write_needs_page_switch(struct buffer_head *bh) {
	return 0;
}
EXPORT_SYMBOL(tenc_write_needs_page_switch);

/*
 * Encrypts the given buffer_head, but uses the dst_page as the destination
 * rather then the one indicated by buffer_head. The dst_page will be
 * the page reserved via page switch mechanism.
 */
void tenc_encrypt_block(struct buffer_head *bh, struct page *dst_page) {

}
EXPORT_SYMBOL(tenc_encrypt_block);

/*
 * Decrypts blocks associated with this page. The should be only one
 * anyway.
 */
void tenc_decrypt_full_page(struct page *page) {

}
EXPORT_SYMBOL(tenc_decrypt_full_page);


/*
 * Decrypts a single file block.
 */
void tenc_decrypt_buffer_head(struct buffer_head *bh) {

}
EXPORT_SYMBOL(tenc_decrypt_buffer_head);
