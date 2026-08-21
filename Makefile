.PHONY: all kernel efi check clean

all: kernel efi

kernel:
	$(MAKE) -C kernel

efi:
	$(MAKE) -C efi

check:
	python3 -m compileall -q tools
	sh -n systemd/insyde-efivarfs-recovery-once

clean:
	$(MAKE) -C kernel clean
	$(MAKE) -C efi clean
