ARCH := x86_64
PE_ARCH := x86-64
CC ?= gcc
LD ?= ld
OBJCOPY ?= objcopy

GNU_EFI_INCLUDE ?= /usr/include/efi
GNU_EFI_LIBDIR ?= /usr/lib64
GNU_EFI_CRT ?= $(GNU_EFI_LIBDIR)/crt0-efi-$(ARCH).o
GNU_EFI_LDS ?= $(GNU_EFI_LIBDIR)/elf_$(ARCH)_efi.lds

BUILD_DIR := build
SOURCE := varprobe.c
OBJECT := $(BUILD_DIR)/varprobe.o
SHARED_OBJECT := $(BUILD_DIR)/varprobe.so
EFI_APPLICATION := $(BUILD_DIR)/uefi-var-probe.efi

CPPFLAGS := \
	-I$(GNU_EFI_INCLUDE) \
	-I$(GNU_EFI_INCLUDE)/$(ARCH) \
	-I$(GNU_EFI_INCLUDE)/protocol \
	-DGNU_EFI_USE_MS_ABI \
	-DEFI_FUNCTION_WRAPPER

CFLAGS := \
	-std=gnu11 \
	-O2 \
	-Wall \
	-Wextra \
	-Werror \
	-fpic \
	-ffreestanding \
	-fno-jump-tables \
	-fno-tree-switch-conversion \
	-fno-stack-protector \
	-fno-stack-check \
	-fshort-wchar \
	-mno-red-zone \
	-maccumulate-outgoing-args

LDFLAGS := \
	-nostdlib \
	-znocombreloc \
	-T $(GNU_EFI_LDS) \
	-shared \
	-Bsymbolic \
	-L$(GNU_EFI_LIBDIR)

.PHONY: all clean

all: $(EFI_APPLICATION)

$(BUILD_DIR):
	mkdir -p $@

$(OBJECT): $(SOURCE) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(SHARED_OBJECT): $(OBJECT)
	$(LD) $(LDFLAGS) $(GNU_EFI_CRT) $< -lgnuefi -lefi -o $@

$(EFI_APPLICATION): $(SHARED_OBJECT)
	$(OBJCOPY) \
		-j .text \
		-j .rodata \
		-j .sdata \
		-j .data \
		-j .dynamic \
		-j .dynsym \
		-j .rel \
		-j .rela \
		-j .reloc \
		--output-target=pei-$(PE_ARCH) \
		--subsystem=efi-app \
		$< $@

clean:
	rm -f $(OBJECT) $(SHARED_OBJECT) $(EFI_APPLICATION)
