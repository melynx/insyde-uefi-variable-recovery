obj-m := insyde_efivarfs_recovery.o

KERNEL_RELEASE := $(shell uname -r)
KERNEL_BUILD ?= /lib/modules/$(KERNEL_RELEASE)/build

.PHONY: all clean

all:
	$(MAKE) -C $(KERNEL_BUILD) M=$(CURDIR) modules

clean:
	$(MAKE) -C $(KERNEL_BUILD) M=$(CURDIR) clean
