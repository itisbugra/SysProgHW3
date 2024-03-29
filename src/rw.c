#include "rw.h"

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <asm/uaccess.h>
#include <asm/switch_to.h>

#include "message_dev.h"
#include "kmessage.h"

#ifndef BUFSIZ
#define BUFSIZ 1024
#endif

struct _kmessaged_parse_result_t {
    char *recipient;
    char *msgdata;
};

static int parse_msg(struct _kmessaged_parse_result_t *res, const char *msgstr)
{
    if (!msgstr || msgstr[0] != '@') {
        printk(KERN_DEBUG "kmessaged: message %s has no recipient\n", msgstr);

        return EINVAL;
    }

    res->recipient = kmalloc(sizeof(char) * KMESSAGED_MAX_USERNAME_LEN, GFP_KERNEL);
    res->msgdata = kmalloc(sizeof(char) * KMESSAGED_MAX_MESSAGE_LEN, GFP_KERNEL);

    if (sscanf(msgstr, "@%s %s\n", res->recipient, res->msgdata) != 2) {
        printk(KERN_DEBUG "kmessaged: message was formatted improperly\n");

        return EINVAL;
    }

    printk(KERN_DEBUG "kmessaged: message parsed recipient: %s message \"%s\"", res->recipient, res->msgdata);

    return 0;
}

ssize_t kmessaged_read(struct file *filp, char __user *usbuf, size_t count, loff_t *f_pos)
{
    struct kmessaged_message_dev_t *dev = filp->private_data;
    int result;
    char *buf = kmalloc(sizeof(char) * BUFSIZ, GFP_KERNEL);
    char *username_buf = kmalloc(sizeof(char) * BUFSIZ, GFP_KERNEL);
    char *sender_buf = kmalloc(sizeof(char) * BUFSIZ, GFP_KERNEL);
    ssize_t it;
    uid_t uid;
    unsigned long unread_cnt;

    printk(KERN_DEBUG "kmessaged: gonna read with %d chars at %d\n", count, *f_pos);

    if (!buf) {
        return -ENOMEM;
    }

    memset(buf, 0, sizeof(char) * BUFSIZ);

    result = -kmessaged_message_dev_resolve_target(dev, username_buf, get_current_cred()->uid);

    if (result) {
        printk(KERN_DEBUG "kmessaged: target could not be resolved with uid: %u\n", get_current_cred()->uid);

        goto on_err;
    }

    for (it = 0; it < dev->unread_cnt; ++it) {
        if (strcmp(dev->unread_msgs[it].recipient, username_buf) == 0) {
            result = -kmessaged_message_dev_resolve_target(dev, sender_buf, dev->unread_msgs[it].uid);

            if (result) {
                printk(KERN_DEBUG "kmessaged: sender could not be resolved with uid: %u\n", dev->unread_msgs[it].uid);

                goto on_err;
            }

            strcat(buf, "* ");
            strcat(buf, sender_buf);
            strcat(buf, ":\t");
            strcat(buf, dev->unread_msgs[it].msg);
            strcat(buf, "\n");
        
            printk(KERN_NOTICE "kmessaged: message found: %s\n", dev->unread_msgs[it].msg);

            memset(sender_buf, 0, sizeof(char) * BUFSIZ);
        }
    }

    if (dev->rdmod == INCLUDE_READ) {
        for (it = 0; it < dev->read_cnt; ++it) {
            if (strcmp(dev->read_msgs[it].recipient, username_buf) == 0) {
                result = -kmessaged_message_dev_resolve_target(dev, sender_buf, dev->read_msgs[it].uid);

                if (result) {
                    printk(KERN_DEBUG "kmessaged: sender could not be resolved with uid: %u\n", dev->read_msgs[it].uid);

                    goto on_err;
                }

                strcat(buf, "  ");
                strcat(buf, sender_buf);
                strcat(buf, ":\t");
                strcat(buf, dev->read_msgs[it].msg);
                strcat(buf, "\n");
            
                printk(KERN_NOTICE "kmessaged: message found: %s\n", dev->read_msgs[it].msg);

                memset(sender_buf, 0, sizeof(char) * BUFSIZ);
            }
        }
    }
    
    if (*f_pos != 0) {
        printk(KERN_NOTICE "kmessaged: *f_pos EOF\n");

        kfree(buf);

        return 0;
    }

    result = kmessaged_message_dev_readall(dev);

    if (result) {
        printk(KERN_NOTICE "kmessaged: mdev couldn't be flushed\n");

        return -EFAULT;
    }

    result = copy_to_user(usbuf, buf, BUFSIZ * sizeof(char));

    if (result) {
        printk(KERN_NOTICE "kmessaged: copy_to_user failed on read\n");

        return -EFAULT;
    }

    kfree(buf);
    kfree(username_buf);

    printk(KERN_DEBUG "kmessaged: gonna report %d chars read\n", BUFSIZ * sizeof(char));

    *f_pos += count;

    return BUFSIZ * sizeof(char);

on_err:
    kfree(buf);
    kfree(username_buf);

    return 0;
}

ssize_t kmessaged_write(struct file *filp, const char __user *usbuf, size_t count, loff_t *f_pos)
{
    struct kmessaged_message_dev_t *dev = filp->private_data;
    int result;
    char *buf = kmalloc(sizeof(char) * count, GFP_KERNEL);
    struct kmessage_t msg;
    struct _kmessaged_parse_result_t pr;
    unsigned long msgcnt;

    printk(KERN_DEBUG "kmessaged: gonna write to %x\n", dev);

    if (!buf) {
        return -ENOMEM;
    }

    msgcnt = dev->unread_cnt;

    if (dev->unread_cnt >= dev->msglmt) {
        printk(KERN_NOTICE "kmessaged: maximum number of unread messages reached\n");

        return -ENOMEM;
    }

    result = copy_from_user(buf, usbuf, count);

    if (result) {
        printk(KERN_DEBUG "kmessaged: error during copy_from_user\n");

        goto bailout;
    }

    result = -parse_msg(&pr, buf);

    if (result) {
        printk(KERN_DEBUG "kmessaged: error during parsing msg\n");

        goto bailout;
    }

    result = -kmessaged_msg_create(&msg, pr.msgdata, get_current_cred()->uid, pr.recipient);
    
    if (result) {
        printk(KERN_DEBUG "kmessaged: error while creating message\n");

        goto bailout;
    }

    result = -kmessaged_message_dev_add(dev, &msg);

    if (result) {
        printk(KERN_DEBUG "kmessaged: error while adding message to the device\n");

        goto bailout;
    }

    *f_pos += count;

bailout:
    kfree(buf);

    return count;
}


