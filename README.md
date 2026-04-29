# USB Device Activity Logger Driver

## 📌 Overview

This project implements a **Linux kernel module** that monitors and logs USB device activity in real time. It detects USB insertion and removal events using the USB notifier mechanism and records relevant details such as Vendor ID, Product ID, connection time, disconnection time, and duration.

The logged data is stored efficiently using a circular buffer and is made accessible to user space through both a **character device interface** and a **sysfs interface**.

---

## 🚀 Features

* Detects USB device **insertion and removal events**
* Logs:

  * Vendor ID
  * Product ID
  * Connection & disconnection timestamps
  * Duration of connection
  * Device status (ACTIVE / REMOVED)
* Uses a **circular buffer** for efficient event storage
* Ensures synchronization using **spinlocks**
* Provides:

  * Character device (`/dev/usblogger`)
  * Sysfs interface (`/sys/kernel/usblogger/latest_event`)
* Includes a **user-space application** to read and clear logs

---

## 🏗️ Project Structure

```
.
├── usb_logger.c      # Kernel module source
├── usb_logger.h      # Header file (structures & definitions)
├── Makefile          # Build configuration for kernel module
├── test_app.c        # User-space application
├── device_driver.pdf # Project report
```

---

## ⚙️ Requirements

* Linux system (Ubuntu recommended)
* Kernel headers installed
* GCC compiler
* Root privileges (for loading kernel module)

---

## 🔧 Build Instructions

### Compile Kernel Module

```bash
make
```

This generates:

```
usb_logger.ko
```

---

## ▶️ Load and Unload Module

### Load module

```bash
make load
```

(Internally runs `insmod` and sets permissions)

### Verify module

```bash
lsmod | grep usb_logger
```

### Check logs

```bash
dmesg | grep "USB Logger"
```

### Unload module

```bash
make unload
```

---

## 🧪 User-Space Application

### Compile

```bash
gcc test_app.c -o test_app
```

### Read USB logs

```bash
./test_app read
```

### Clear logs

```bash
./test_app clear
```

---

## 📂 Interfaces

### Character Device

* Path: `/dev/usblogger`
* Used for:

  * Reading USB event logs
  * Clearing stored logs

### Sysfs Interface

```bash
cat /sys/kernel/usblogger/latest_event
```

Displays the most recent USB event.

---

## 🧠 Working Principle

* The driver registers with the **USB subsystem** using a notifier.
* On USB insertion:

  * Vendor ID & Product ID are captured
  * Connection time is recorded
* On USB removal:

  * Matching device is identified
  * Disconnection time and duration are computed
* Events are stored in a **circular buffer**
* A **workqueue** is used to safely handle operations outside atomic context
* Synchronization is ensured using **spinlocks**

---

## 🧪 Testing

* USB devices (e.g., pendrive) were inserted and removed
* Verified using:

```bash
dmesg | grep "USB Logger"
```

* Logs successfully showed:

  * Insert and remove events
  * Correct timestamps and device details

---

## 📌 Conclusion

This project demonstrates practical implementation of a Linux kernel module for USB activity monitoring. It covers key concepts such as kernel-user space communication, synchronization, workqueues, and device driver development.

---

## 👨‍💻 Authors

* Jaikumar Wath
* Ayush Pareek

Group 36 – KernelX
Operating Systems (CS F372)
BITS Pilani, K K Birla Goa Campus

---

## 📄 License

This project is for academic purposes.
