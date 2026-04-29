obj-m += usb_logger.o

KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

load:
	sudo insmod usb_logger.ko
	sudo chmod 666 /dev/usblogger

unload:
	sudo rmmod usb_logger
