#ifndef USB_LOGGER_H
#define USB_LOGGER_H

#include <linux/types.h>
#include <linux/usb.h>

#define MAX_EVENTS 100
#define EVENT_STATUS_ACTIVE 1
#define EVENT_STATUS_REMOVED 0

// Structure to hold USB event data
struct usb_event {
    uint16_t vendor_id;
    uint16_t product_id;
    const struct usb_device *device_ptr; // Bulletproof matching for disconnects
    char device_node[16];                // Placeholder for block layer assignment
    time64_t connect_time;
    time64_t disconnect_time;
    long duration_seconds;
    int status;                          // 1 = ACTIVE, 0 = REMOVED
};

// Circular buffer structure
struct usb_event_buffer {
    struct usb_event events[MAX_EVENTS];
    int head;
    int tail;
    int count;
    spinlock_t lock; 
};

#endif // USB_LOGGER_H
