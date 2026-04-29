#include <linux/module.h>
#include <linux/init.h>
#include <linux/usb.h>
#include <linux/workqueue.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/ktime.h>
#include <linux/sysfs.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/rtc.h>
#include "usb_logger.h"

#define DRIVER_AUTHOR "Group 36 - KernelX"
#define DRIVER_DESC "USB Device Activity Logger"
#define DEVICE_NAME "usblogger"

// IST is UTC +5:30 (5 hours * 3600 + 30 mins * 60 = 19800 seconds)
#define IST_OFFSET_SECONDS 19800 

static int major_number;
static struct class *logger_class;
static struct usb_event_buffer event_buf;
static struct workqueue_struct *usb_wq;

struct usb_work_data {
    struct work_struct work;
    struct usb_device *udev;
    unsigned long action;
};

// --- HELPER: FORMAT TIME (Now in IST) ---
static void format_time(time64_t ts, char *buf, size_t size) {
    struct rtc_time tm;
    
    // Add the IST offset to the raw UTC timestamp
    time64_t ts_ist = ts + IST_OFFSET_SECONDS; 
    
    rtc_time64_to_tm(ts_ist, &tm);
    scnprintf(buf, size, "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
}

// --- SYSFS INTERFACE ---
static ssize_t latest_event_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf) {
    int idx;
    ssize_t len = 0;
    char conn_time_str[16];
    
    spin_lock(&event_buf.lock);
    if (event_buf.count == 0) {
        spin_unlock(&event_buf.lock);
        return scnprintf(buf, PAGE_SIZE, "No USB events logged yet.\n");
    }
    
    idx = (event_buf.head - 1 + MAX_EVENTS) % MAX_EVENTS;
    struct usb_event *e = &event_buf.events[idx];
    
    format_time(e->connect_time, conn_time_str, sizeof(conn_time_str));
    
    len += scnprintf(buf, PAGE_SIZE, "Vendor ID: %04x\nProduct ID: %04x\nConnection Time (IST): %s\nStatus: %s\n",
                   e->vendor_id, e->product_id, conn_time_str,
                   e->status == EVENT_STATUS_ACTIVE ? "ACTIVE" : "REMOVED");

    if (e->status == EVENT_STATUS_REMOVED) {
        char disc_time_str[16];
        format_time(e->disconnect_time, disc_time_str, sizeof(disc_time_str));
        len += scnprintf(buf + len, PAGE_SIZE - len, "Disconnection Time (IST): %s\nDuration: %ld seconds\n", 
                       disc_time_str, e->duration_seconds);
    }
    
    spin_unlock(&event_buf.lock);
    return len;
}
static struct kobj_attribute latest_event_attr = __ATTR_RO(latest_event);
static struct kobject *logger_kobj;

// --- CHARACTER DEVICE OPERATIONS ---
static ssize_t logger_read(struct file *file, char __user *user_buffer, size_t size, loff_t *offset) {
    char *kbuf;
    struct usb_event *local_events;
    int len = 0, i, local_count;
    ssize_t bytes_to_copy;
    size_t alloc_size;

    if (*offset > 0) return 0; 

    // 1. Safely grab the count
    spin_lock(&event_buf.lock);
    local_count = event_buf.count;
    spin_unlock(&event_buf.lock);

    if (local_count == 0) {
        char *empty_msg = "No USB activity logged.\n";
        bytes_to_copy = min((size_t)strlen(empty_msg), size);
        if (copy_to_user(user_buffer, empty_msg, bytes_to_copy)) return -EFAULT;
        *offset += bytes_to_copy;
        return bytes_to_copy;
    }

    alloc_size = max((size_t)256, (size_t)(local_count * 300));
    
    // 2. Allocate memory outside the lock
    kbuf = vzalloc(alloc_size); 
    local_events = kmalloc_array(local_count, sizeof(struct usb_event), GFP_KERNEL);
    
    if (!kbuf || !local_events) {
        if (kbuf) vfree(kbuf);
        if (local_events) kfree(local_events);
        return -ENOMEM;
    }

    // 3. Snapshot the data FAST to minimize lock contention
    spin_lock(&event_buf.lock);
    for (i = 0; i < local_count; i++) {
        int idx = (event_buf.tail + i) % MAX_EVENTS;
        local_events[i] = event_buf.events[idx];
    }
    spin_unlock(&event_buf.lock);

    // 4. Format strings leisurely OUTSIDE the spinlock
    for (i = 0; i < local_count; i++) {
        char conn_time_str[16], disc_time_str[16];
        struct usb_event *e = &local_events[i];

        format_time(e->connect_time, conn_time_str, sizeof(conn_time_str));

        len += scnprintf(kbuf + len, alloc_size - len,
            "[EVENT %d]\nVendor ID: %04x\nProduct ID: %04x\nDevice Node: %s\nConnection Time (IST): %s\n",
            i + 1, e->vendor_id, e->product_id, e->device_node, conn_time_str);

        if (e->status == EVENT_STATUS_REMOVED) {
            format_time(e->disconnect_time, disc_time_str, sizeof(disc_time_str));
            long mins = e->duration_seconds / 60;
            long secs = e->duration_seconds % 60;
            len += scnprintf(kbuf + len, alloc_size - len,
                "Disconnection Time (IST): %s\nDuration: %ldm %lds\nStatus: REMOVED\n\n",
                disc_time_str, mins, secs);
        } else {
            len += scnprintf(kbuf + len, alloc_size - len, "Status: ACTIVE\n\n");
        }
    }

    // 5. Send to user space
    bytes_to_copy = min((size_t)len, size);
    if (copy_to_user(user_buffer, kbuf, bytes_to_copy)) {
        vfree(kbuf);
        kfree(local_events);
        return -EFAULT;
    }

    *offset += bytes_to_copy;
    
    // 6. Cleanup
    vfree(kbuf);
    kfree(local_events);
    return bytes_to_copy;
}

static ssize_t logger_write(struct file *file, const char __user *user_buffer, size_t size, loff_t *offset) {
    spin_lock(&event_buf.lock);
    event_buf.head = 0;
    event_buf.tail = 0;
    event_buf.count = 0;
    spin_unlock(&event_buf.lock);
    printk(KERN_INFO "USB Logger: Logs cleared.\n");
    return size;
}

static struct file_operations logger_fops = {
    .owner = THIS_MODULE,
    .read = logger_read,
    .write = logger_write,
};

// --- WORKQUEUE HANDLER ---
static void usb_work_handler(struct work_struct *work) {
    struct usb_work_data *data = container_of(work, struct usb_work_data, work);
    struct usb_device *udev = data->udev;
    uint16_t vid = le16_to_cpu(udev->descriptor.idVendor);
    uint16_t pid = le16_to_cpu(udev->descriptor.idProduct);
    int i, idx;

    spin_lock(&event_buf.lock);

    if (data->action == USB_DEVICE_ADD) {
        struct usb_event *new_event = &event_buf.events[event_buf.head];
        new_event->vendor_id = vid;
        new_event->product_id = pid;
        new_event->device_ptr = udev; // Perfect matching pointer
        new_event->connect_time = ktime_get_real_seconds();
        new_event->status = EVENT_STATUS_ACTIVE;
        
        scnprintf(new_event->device_node, sizeof(new_event->device_node), "N/A (Block)");

        event_buf.head = (event_buf.head + 1) % MAX_EVENTS;
        if (event_buf.count < MAX_EVENTS) {
            event_buf.count++;
        } else {
            event_buf.tail = (event_buf.tail + 1) % MAX_EVENTS; // Overwrite oldest
        }
        printk(KERN_INFO "USB Logger: Inserted - VID: %04x, PID: %04x\n", vid, pid);

    } else if (data->action == USB_DEVICE_REMOVE) {
        for (i = 0; i < event_buf.count; i++) {
            idx = (event_buf.head - 1 - i + MAX_EVENTS) % MAX_EVENTS; 
            // Match using the exact device pointer
            if (event_buf.events[idx].vendor_id == vid &&
                event_buf.events[idx].product_id == pid &&
                event_buf.events[idx].device_ptr == udev && 
                event_buf.events[idx].status == EVENT_STATUS_ACTIVE) {
                
                event_buf.events[idx].status = EVENT_STATUS_REMOVED;
                event_buf.events[idx].disconnect_time = ktime_get_real_seconds();
                event_buf.events[idx].duration_seconds = 
                    event_buf.events[idx].disconnect_time - event_buf.events[idx].connect_time;
                break;
            }
        }
        printk(KERN_INFO "USB Logger: Removed - VID: %04x, PID: %04x\n", vid, pid);
    }

    spin_unlock(&event_buf.lock);
    kfree(data);
}

// --- USB NOTIFIER ---
static int usb_notify_callback(struct notifier_block *nb, unsigned long action, void *dev) {
    struct usb_work_data *work_data;
    
    if (action != USB_DEVICE_ADD && action != USB_DEVICE_REMOVE) {
        return NOTIFY_DONE;
    }

    work_data = kmalloc(sizeof(*work_data), GFP_ATOMIC);
    if (!work_data) return NOTIFY_BAD;

    INIT_WORK(&work_data->work, usb_work_handler);
    work_data->udev = (struct usb_device *)dev;
    work_data->action = action;

    queue_work(usb_wq, &work_data->work);
    return NOTIFY_OK;
}

static struct notifier_block usb_nb = {
    .notifier_call = usb_notify_callback,
};

// --- MODULE INIT / EXIT ---
static int __init logger_init(void) {
    printk(KERN_INFO "USB Logger: Initializing...\n");
    
    spin_lock_init(&event_buf.lock);
    event_buf.head = 0;
    event_buf.tail = 0;
    event_buf.count = 0;

    usb_wq = alloc_workqueue("usb_logger_wq", WQ_UNBOUND, 1);
    usb_register_notify(&usb_nb);

    major_number = register_chrdev(0, DEVICE_NAME, &logger_fops);
    logger_class = class_create(THIS_MODULE, "logger_class");
    device_create(logger_class, NULL, MKDEV(major_number, 0), NULL, DEVICE_NAME);

    // FIRST create kobject
    logger_kobj = kobject_create_and_add("usblogger", kernel_kobj);
    if (!logger_kobj) {
        printk(KERN_ERR "USB Logger: Failed to create kobject\n");
        return -ENOMEM;
    }

    // THEN create sysfs file attached to it
    if (sysfs_create_file(logger_kobj, &latest_event_attr.attr)) {
        printk(KERN_ERR "USB Logger: Failed to create sysfs file\n");
    }

    return 0;
}

static void __exit logger_exit(void) {
    printk(KERN_INFO "USB Logger: Exiting...\n");
    usb_unregister_notify(&usb_nb);
    flush_workqueue(usb_wq);
    destroy_workqueue(usb_wq);

    kobject_put(logger_kobj);
    device_destroy(logger_class, MKDEV(major_number, 0));
    class_destroy(logger_class);
    unregister_chrdev(major_number, DEVICE_NAME);
}

module_init(logger_init);
module_exit(logger_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
