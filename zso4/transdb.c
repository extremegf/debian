/*
 * Module handles the device creation and file operations.
 *
 * Author: Przemyslaw Horban <p.horban@mimuw.edu.pl>
 */

#include <linux/module.h>
#include <asm/uaccess.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/major.h>
#include <linux/kernel.h> /* KERN_WARNING */

static int transdb_init_module(void) {
    printk(KERN_WARNING "Read from me!\n");

	return 0;
}

static void transdb_cleanup_module(void) {
	printk(KERN_WARNING "unregister_chrdev succeeded\n");
}

module_init(transdb_init_module);
module_exit(transdb_cleanup_module);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Przemyslaw Horban <p.horban@mimuw.edu.pl>");
MODULE_DESCRIPTION("Transactional database in the kernel.");


