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

#define show_int(name) printk(KERN_INFO #name " = %d\n", name);

static int transdb_open(struct inode *ino, struct file *filep)
{
    printk(KERN_INFO "transdb_open()\n");
    filep->private_data = NULL;
    return 0;
}

static int transdb_release(struct inode *ino, struct file *filep)
{
    printk(KERN_INFO "transdb_release()\n");
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
        printk(KERN_INFO "new_trans_context()=%p\n", filp->private_data);
    }
    return filp->private_data;
}

static ssize_t transdb_rw(rw_t rw, struct file *filp,
                          char __user *buf, const char __user *const_buf,
                          size_t count, loff_t *f_pos)
{
    size_t seg_nr = *f_pos / SEGMENT_SIZE;
    size_t ofs_in_seg = *f_pos % SEGMENT_SIZE;
    size_t len_in_seg = SEGMENT_SIZE - ofs_in_seg;
    size_t copy_len = min(len_in_seg, count);
    size_t copied = 0;
    struct trans_context_t *trans = open_trans_if_needed(filp);

    printk(KERN_INFO "We got a transdb_rw trans=%p\n", trans);

    if (!trans) {
        return -ENOMEM;
    }

    printk(KERN_INFO "transacion was created\n");

    while (copy_len > 0) {
        char *seg_data;
        size_t not_copied;

        if (rw == TDB_READ) {
            seg_data = get_read_segment(trans, seg_nr);
            not_copied = copy_to_user(buf, seg_data + ofs_in_seg, copy_len);

        } else {
            seg_data = get_write_segment(trans, seg_nr);
            not_copied = copy_from_user(seg_data + ofs_in_seg, const_buf,
                                        copy_len);
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

    printk(KERN_INFO "return copied = %d\n", copied);
    return copied;
}

static ssize_t transdb_read(struct file *filp, char __user *buf, size_t count,
                            loff_t *f_pos)
{
    printk(KERN_INFO "read(count=%d, f_pos=%d)\n", (int)count, (int)*f_pos);
    return transdb_rw(TDB_READ, filp, buf, NULL, count, f_pos);
}

static ssize_t transdb_write(struct file *filp, const char __user *buf,
                             size_t count, loff_t *f_pos)
{
    printk(KERN_INFO "write(count=%d, f_pos=%d)\n", (int)count, (int)*f_pos);
    return transdb_rw(TDB_WRITE, filp, NULL, buf, count, f_pos);
}

long transdb_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    int retval = 0;

    printk(KERN_INFO "transdb_ioctl(type=%c, cmd=%d)\n",
           _IOC_TYPE(cmd), _IOC_NR(cmd));

    // Extract the type and number bitfields, and don't decode
    // wrong cmds: return ENOTTY (inappropriate ioctl).
    if (_IOC_TYPE(cmd) != _TRANSDB_IO_MAGIC) return -ENOTTY;
    if (_IOC_NR(cmd) != 0x31 && _IOC_NR(cmd) != 0x32) return -ENOTTY;

    switch(cmd) {
    case DB_COMMIT:
        retval = EDEADLK;
        if (filp->private_data) {
            if(finish_transaction(COMMIT, filp->private_data) == COMMIT) {
                retval = 0;
            }
        }
        break;

    case DB_ROLLBACK:
        finish_transaction(ROLLBACK, filp->private_data);
        retval = 0;
        break;

    default:  /* redundant, as cmd was checked against MAXNR */
        return -ENOTTY;
    }

    return retval;
}

static loff_t transdb_llseek(struct file *filp, loff_t off, int whence)
{
    struct scull_dev *dev = filp->private_data;
    loff_t newpos;

    switch(whence) {
    case 0: /* SEEK_SET */
        newpos = off;
        break;

    case 1: /* SEEK_CUR */
        newpos = filp->f_pos + off;
        break;

    case 2: /* SEEK_END */
        // Not supported.
        return -EINVAL;
        break;

    default: /* can't happen */
        return -EINVAL;
    }
    if (newpos < 0) return -EINVAL;
    filp->f_pos = newpos;
    return newpos;
}

static const struct file_operations db_fops = {
    .owner = THIS_MODULE,
    .open = transdb_open,
    .llseek = transdb_llseek,
    .read = transdb_read,
    .write = transdb_write,
    .release = transdb_release,
    .unlocked_ioctl = transdb_ioctl,
    .compat_ioctl = transdb_ioctl,
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

    printk(KERN_INFO "transdb module inserted.\n");

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
    printk(KERN_INFO "transdb module removed.\n");
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

