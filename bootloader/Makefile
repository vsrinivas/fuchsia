# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

ARCH		:= x86_64

# Temporary ugliness until we integrate gigaboot build into magenta build
LK_ROOT         := ..
LKMAKEROOT      := $(LK_ROOT)
-include $(LK_ROOT)/prebuilt/config.mk

EFI_TOOLCHAIN	:= $(LK_ROOT)/prebuilt/$(ARCH_x86_64_TOOLCHAIN_PREFIX)
EFI_CC		:= $(EFI_TOOLCHAIN)gcc
EFI_LD		:= $(EFI_TOOLCHAIN)ld
EFI_READELF	:= $(EFI_TOOLCHAIN)readelf
EFI_OBJCOPY	:= $(EFI_TOOLCHAIN)objcopy
EFI_AR		:= $(EFI_TOOLCHAIN)ar
EFI_NM          := $(EFI_TOOLCHAIN)nm

EFI_LINKSCRIPT	:= build/efi-x86-64.lds

EFI_CFLAGS	:= -fPIE -fshort-wchar -fno-stack-protector -mno-red-zone
EFI_CFLAGS	+= -Wall -std=c99
EFI_CFLAGS	+= -ffreestanding -nostdinc -Iinclude -Isrc
EFI_CFLAGS      += -I$(LK_ROOT)/system/public

EFI_LDFLAGS	:= -nostdlib -T $(EFI_LINKSCRIPT) -pie
EFI_LDFLAGS	+= -Lout

EFI_LIBS	:= -lxefi

what_to_build::	all

# build rules and macros
include build/build.mk

# declare applications here
#$(call efi_app, hello, hello.c)
$(call efi_app, showmem, src/showmem.c)
$(call efi_app, fileio, src/fileio.c)
$(call efi_app, usbtest, src/usbtest.c)

OSBOOT_FILES := src/osboot.c src/cmdline.c src/magenta.c src/netboot.c
OSBOOT_FILES += src/netifc.c src/inet6.c src/pci.c src/framebuffer.c
$(call efi_app, osboot, $(OSBOOT_FILES))

ifneq ($(APP),)
	APP := out/$(APP).efi
else
	APP := out/osboot.efi
endif

LIB_SRCS := \
    lib/efi/guids.c \
    lib/xefi.c \
    lib/loadfile.c \
    lib/console-printf.c \
    lib/ctype.c \
    lib/printf.c \
    lib/stdlib.c \
    lib/string.c

LIB_OBJS := $(patsubst %.c,out/%.o,$(LIB_SRCS))
DEPS += $(patsubst %.c,out/%.d,$(LIB_SRCS))

out/libxefi.a: $(LIB_OBJS)
	@mkdir -p $(dir $@)
	@echo archiving: $@
	$(QUIET)rm -f $@
	$(QUIET)$(EFI_AR) rc $@ $^

out/BOOTx64.EFI: $(APP)
	@mkdir -p $(dir $@)
	$(QUIET)cp -f $^ $@

# generate a small IDE disk image for qemu
out/disk.img: $(APPS) out/BOOTx64.EFI
	@mkdir -p $(dir $@)
	$(QUIET)./build/mkdiskimg.sh $@
	@echo copying: $(APPS) README.txt to disk.img
	$(QUIET)mcopy -o -i out/disk.img@@1024K $(APPS) README.txt ::
	$(QUIET)mcopy -o -i out/disk.img@@1024K $(APPS) out/BOOTx64.EFI ::EFI/BOOT/

#disable disk.img by default; doesn't build on macOS
#ALL += out/disk.img

-include $(DEPS)

QEMU_OPTS := -cpu qemu64
QEMU_OPTS += -bios ../prebuilt/downloads/firmware/ovmf/OVMF.fd
QEMU_OPTS += -drive file=out/disk.img,format=raw,if=ide
QEMU_OPTS += -serial stdio
QEMU_OPTS += -m 256M
ifneq ($(USBDEV),)
    QEMU_OPTS += -usbdevice host:$(USBDEV)
endif

qemu-e1000: QEMU_OPTS += -netdev type=tap,ifname=qemu,script=no,id=net0 -net nic,model=e1000,netdev=net0
qemu-e1000: all
	qemu-system-x86_64 $(QEMU_OPTS)

qemu: QEMU_OPTS += -net none
qemu:: all
	qemu-system-x86_64 $(QEMU_OPTS)

all: $(ALL)

clean::
	rm -rf out

all-clean: clean
