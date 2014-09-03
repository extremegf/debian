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

#include "transaction.h"
#include "transdb.h"

typedef enum { TDB_READ, TDB_WRITE } rw_t;

static int transdb_open(struct inode *ino, struct file *filep)
{
    filep->private_data = NULL;
    return 0;
}

static int transdb_release(struct inode *ino, struct file *filep)
{
    if (filep->private_data != NULL) {
        finish_transaction(ROLLBACK, filep->private_data);
        filep->private_data = NULL;
    }
    return 0;
}

//static size_t min(size_t a, size_t)

static struct trans_context_t *open_trans_if_needed(struct file *filp)
{
    if (!filp->private_data) {
        filp->private_data = new_trans_context();
    }
    return filp->private_data;
}

static ssize_t transdb_rw(rw_t rw, struct file *filp, char __user *buf,
                          size_t count, loff_t *f_pos)
{
    size_t seg_nr = *f_pos / SEGMENT_SIZE;
    size_t ofs_in_seg = *f_pos % SEGMENT_SIZE;
    size_t len_in_seg = SEGMENT_SIZE - ofs_in_seg;
    size_t copy_len = min(len_in_seg, count);
    size_t copied = 0;
    struct trans_context_t *trans = open_trans_if_needed(filp);

    if (!trans) {
        return -ENOMEM;
    }

    while (copy_len > 0) {
        char *seg_data;
        size_t not_copied;

        if (rw == TDB_READ) {
            seg_data = get_read_segment(trans, seg_nr);
            not_copied = copy_to_user(buf, seg_data + ofs_in_seg, copy_len);
        }
        else {
        	seg_data = get_write_segment(trans, seg_nr);
            not_copied = copy_from_user(seg_data + ofs_in_seg, buf, copy_len);
        }

        if (not_copied) {
        	return copied + (copy_len - not_copied);
        }

        count -= copy_len;
        buf += copy_len;
        *f_pos += copy_len;
        copied += copy_len;

        seg_nr = *f_pos / SEGMENT_SIZE;
        ofs_in_seg = *f_pos % SEGMENT_SIZE;
        len_in_seg = SEGMENT_SIZE - ofs_in_seg;
        copy_len = min(len_in_seg, count);
    }

    return copied;
}

static ssize_t transdb_read(struct file *filp, char __user *buf, size_t count,
                            loff_t *f_pos)
{
	return transdb_rw(TDB_READ, filp, buf, count, f_pos);
}

static ssize_t transdb_write(struct file *filp, char __user *buf, size_t count,
                            loff_t *f_pos)
{
	return transdb_rw(TDB_WRITE, filp, buf, count, f_pos);
}

static const struct file_operations db_fops = {
owner:
    THIS_MODULE,
read:
    transdb_read,
write:
    transdb_write,
open:
    transdb_open,
release:
    transdb_release,
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


/* Design
read/write
gdy private_data == NULL alokuje trans_context i init_trans_context
get_read_segment, get_write_segment do kopiowania z i do bufora
na locku
ioctl()
woła finish_transaction i ustawia private_data

*/

