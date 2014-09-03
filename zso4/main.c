/*
 * Module handles the device creation and file operations.
 *
 * Author: Przemyslaw Horban <p.horban@mimuw.edu.pl>
 */

#include <asm/uaccess.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h> /* KERN_WARNING */
#include <linux/major.h>
#include <linux/miscdevice.h>
#include <linux/module.h>

static ssize_t hello_read(struct file * file, char * buf,
                          size_t count, loff_t *ppos)
{
    char *hello_str = "Hello, world!\n";
    int len = strlen(hello_str); /* Don't include the null byte. */
    /*
     * We only support reading the whole string at once.
     */
    if (count < len)
        return -EINVAL;
    /*
     * If file position is non-zero, then assume the string has
     * been read and indicate there is no more data to be read.
     */
    if (*ppos != 0)
        return 0;
    /*
     * Besides copying the string to the user provided buffer,
     * this function also checks that the user has permission to
     * write to the buffer, that it is mapped, etc.
     */
    if (copy_to_user(buf, hello_str, len))
        return -EINVAL;
    /*
     * Tell the user how much data we wrote.
     */
    *ppos = len;

    return len;
}

static const struct file_operations db_fops = {
owner:
    THIS_MODULE,
read:
    hello_read,
};


static struct miscdevice db_device = {
    /*
     * We don't care what minor number we end up with, so tell the
     * kernel to just pick one.
     */
    MISC_DYNAMIC_MINOR,
    /*
     * Name ourselves /dev/db.
     */
    "db",
    /*
     * What functions to call when a program performs file
     * operations on the device.
     */
    &db_fops
};

static int transdb_init_module(void)
{
    int ret;

    ret = trans_init();
    if (ret)
        printk(KERN_ERR "Unable initialize transactions (out of mem?)\n");

    /*
     * Create the "db" device in the /sys/class/misc directory.
     * Udev will automatically create the /dev/db device using
     * the default rules.
     */
    ret = misc_register(&db_device);
    if (ret)
        printk(KERN_ERR "Unable to register /dev/db device\n");

    return ret;
}

static void transdb_cleanup_module(void)
{
    misc_deregister(&db_device);
    trans_destroy();
}

module_init(transdb_init_module);
module_exit(transdb_cleanup_module);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Przemyslaw Horban <p.horban@mimuw.edu.pl>");
MODULE_DESCRIPTION("Transactional database in the kernel.");


