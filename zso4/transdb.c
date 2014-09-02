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

module_init(yatb_init_module);
module_exit(yatb_cleanup_module);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Przemyslaw Horban <p.horban@mimuw.edu.pl>");
MODULE_DESCRIPTION("Transactional database in the kernel.");


