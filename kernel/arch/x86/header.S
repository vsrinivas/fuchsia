// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2016 Google, Inc.
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT


#include <asm.h>
#include <arch/x86/asm.h>

/* The magic number for the Multiboot header. */
#define MULTIBOOT_HEADER_MAGIC 0x1BADB002

/* The flags for the Multiboot header. */
#define MULTIBOOT_HEADER_FLAGS 0x00010002



#include <magenta/boot/bootdata.h>

.section .text.boot0, "ax", @progbits

LOCAL_DATA(_setup_start)

#if ENABLE_NEW_BOOTDATA
    /* bootdata file header */
    .int BOOTDATA_CONTAINER
    .int PHYS(__data_end) - PHYS_LOAD_ADDRESS - 32
    .int BOOTDATA_MAGIC
    .int BOOTDATA_FLAG_EXTRA
    .int 0
    .int 0
    .int BOOTITEM_MAGIC
    .int BOOTITEM_NO_CRC32

    /* bootdata kernel header */
    .int BOOTDATA_KERNEL
    .int PHYS(__data_end) - PHYS_LOAD_ADDRESS - 64
    .int 0
    .int BOOTDATA_FLAG_EXTRA
    .int 0
    .int 0
    .int BOOTITEM_MAGIC
    .int BOOTITEM_NO_CRC32
#else
    /* bootdata file header */
    .int BOOTDATA_CONTAINER
    .int PHYS(__data_end) - PHYS_LOAD_ADDRESS - 16
    .int BOOTDATA_MAGIC
    .int 0

    /* bootdata kernel header */
    .int BOOTDATA_KERNEL
    .int PHYS(__data_end) - PHYS_LOAD_ADDRESS - 32
    .int 0
    .int 0
#endif
    .quad PHYS(_entry64)
    .quad 0



.align 8
LOCAL_DATA(_multiboot_header)
    /* magic */
    .int MULTIBOOT_HEADER_MAGIC
LOCAL_DATA(_multiboot_flags)
    /* flags */
    .int MULTIBOOT_HEADER_FLAGS
LOCAL_DATA(_multiboot_checksum)
    /* checksum */
    .int -(MULTIBOOT_HEADER_MAGIC + MULTIBOOT_HEADER_FLAGS)

LOCAL_DATA(_multiboot_header_addr)
    /* header_addr */
    .int PHYS_LOAD_ADDRESS - PHYS_HEADER_LOAD_OFFSET + (_multiboot_header - _setup_start)
LOCAL_DATA(_multiboot_load_addr)
    /* load_addr */
    .int PHYS_LOAD_ADDRESS - PHYS_HEADER_LOAD_OFFSET
LOCAL_DATA(_multiboot_load_end_addr)
    /* load_end_addr */
    .int PHYS(__data_end)
LOCAL_DATA(_multiboot_bss_end_addr)
    /* bss_end_addr */
    .int PHYS(__bss_end)
LOCAL_DATA(_multiboot_entry)
    /* entry_addr */
    .int PHYS(_multiboot_start)
