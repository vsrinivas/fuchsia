// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#ifndef __ASSEMBLER__
#include <zircon/compiler.h>
#include <stdint.h>
#endif

// lsw of sha256("bootdata")
#define BOOTDATA_MAGIC (0x868cf7e6)

// lsw of sha256("bootitem")
#define BOOTITEM_MAGIC (0xb5781729)

// Round n up to the next 8 byte boundary
#define BOOTDATA_ALIGN(n) (((n) + 7) & (~7))

#define BOOTITEM_NO_CRC32 (0x4a87e8d6)

// This flag is required.
#define BOOTDATA_FLAG_V2         (0x00010000)

// Bootdata items with the CRC32 flag must have a valid crc32.
// Otherwise their crc32 field must contain BOOTITEM_NO_CRC32
#define BOOTDATA_FLAG_CRC32      (0x00020000)

// Containers are used to wrap a set of bootdata items
// written to a file or partition.  The "length" is the
// length of the set of following bootdata items.  The
// "extra" is the value BOOTDATA_MAGIC and "flags" is
// set to 0.
#define BOOTDATA_CONTAINER        (0x544f4f42) // BOOT

// BOOTFS images.  The "extra" field is the decompressed
// size of the image, if compressed, otherwise the same
// as the "length" field.
#define BOOTDATA_BOOTFS_BOOT      (0x42534642) // BFSB
#define BOOTDATA_BOOTFS_SYSTEM    (0x53534642) // BFSS
#define BOOTDATA_BOOTFS_DISCARD   (0x58534642) // BFSX

#define BOOTDATA_BOOTFS_MASK      (0x00FFFFFF)
#define BOOTDATA_BOOTFS_TYPE      (0x00534642) // BFS\0

// Virtual disk images.  The header fields and compression protocol
// are the same as for the BOOTFS types, but the payload before
// compression is a raw disk image rather than BOOTFS format.
#define BOOTDATA_RAMDISK          (0x4b534452) // RDSK

// A Zircon Kernel Image
// Content: bootdata_kernel_t
#define BOOTDATA_KERNEL           (0x4c4e524b) // KRNL

// A Zircon Partition Map
// Content: bootdata_partition_map_t
#define BOOTDATA_PARTITION_MAP    (0x54524150) // PART

// Flag indicating that the bootfs is compressed.
#define BOOTDATA_BOOTFS_FLAG_COMPRESSED  (1 << 0)


// These items are for passing from bootloader to kernel

// Kernel Command Line String
// Content: uint8_t[]
#define BOOTDATA_CMDLINE          (0x4c444d43) // CMDL

// ACPI Root Table Pointer
// Content: uint64_t phys addr
#define BOOTDATA_ACPI_RSDP        (0x50445352) // RSDP

// SMBIOS entry point pointer
// Content: uint64_t phys addr
#define BOOTDATA_SMBIOS           (0x49424d53) // SMBI

// Framebuffer Parameters
// Content: bootdata_swfb_t
#define BOOTDATA_FRAMEBUFFER      (0x42465753) // SWFB

// Debug Serial Port
// Content: bootdata_uart_t
#define BOOTDATA_DEBUG_UART       (0x54524155) // UART

// Platform ID Information
// Content: bootdata_platform_id_t
#define BOOTDATA_PLATFORM_ID      (0x44494C50) // PLID

// Memory which will persist across warm boots
// Content bootdata_lastlog_nvram_t
#define BOOTDATA_LASTLOG_NVRAM    (0x4c4c564e) // NVLL

// This reflects a typo we need to support for a while
#define BOOTDATA_LASTLOG_NVRAM2   (0x4c4c5643) // CVLL

// E820 Memory Table
// Content: e820entry[]
#define BOOTDATA_E820_TABLE       (0x30323845) // E820

// EFI Memory Map
// Content: a uint64_t entrysz followed by a set of
// efi_memory_descriptor aligned on entrysz
#define BOOTDATA_EFI_MEMORY_MAP   (0x4d494645) // EFIM

// EFI System Table
// Content: a uint64_t physical address of the table
#define BOOTDATA_EFI_SYSTEM_TABLE (0x53494645) // EFIS

// Last crashlog
// Content: ascii/utf8 log data from previous boot
#define BOOTDATA_LAST_CRASHLOG    (0x4d4f4f42) // BOOM

// CPU configuration
// Content: bootdata_cpu_config_t
#define BOOTDATA_CPU_CONFIG       (0x43555043) // CPUC

// Memory configuration
// Content: one or more of bootdata_mem_range_t (count determined by bootdata_t length)
#define BOOTDATA_MEM_CONFIG       (0x434D454D) // MEMC

// Kernel driver configuration
// Content: driver specific struct, with type determined by bootdata "extra" field
#define BOOTDATA_KERNEL_DRIVER    (0x5652444B) // KDRV

#define BOOTDATA_IGNORE           (0x50494b53) // SKIP

#ifndef __ASSEMBLER__
__BEGIN_CDECLS;

// BootData header, describing the type and size of data
// used to initialize the system. All fields are little-endian.
//
// BootData headers in a stream must be 8-byte-aligned.
//
// The length field specifies the actual payload length
// and does not include the size of padding.
typedef struct {
    // Boot data type
    uint32_t type;

    // Size of the payload following this header
    uint32_t length;

    // type-specific extra data
    // For CONTAINER this is MAGIC.
    // For BOOTFS this is the decompressed size.
    uint32_t extra;

    // Flags for the boot data. See flag descriptions for each type.
    uint32_t flags;

    // For future expansion.  Set to 0.
    uint32_t reserved0;
    uint32_t reserved1;

    // Must be BOOTITEM_MAGIC
    uint32_t magic;

    // Must be the CRC32 of payload if FLAG_CRC32 is set,
    // otherwise must be BOOTITEM_NO_CRC32
    uint32_t crc32;
} bootdata_t;

typedef struct {
    uint64_t base; // physical base addr
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t format;
} bootdata_swfb_t;

typedef struct {
    uint64_t entry64;
    uint64_t reserved;
} bootdata_kernel_t;

typedef struct {
    bootdata_t hdr_file;
    bootdata_t hdr_kernel;
    bootdata_kernel_t data_kernel;
} zircon_kernel_t;

#define BOOTDATA_PART_NAME_LEN 32
#define BOOTDATA_PART_GUID_LEN 16

typedef struct {
    uint8_t type_guid[BOOTDATA_PART_GUID_LEN];
    uint8_t uniq_guid[BOOTDATA_PART_GUID_LEN];
    uint64_t first_block;
    uint64_t last_block;
    uint64_t flags;
    char name[BOOTDATA_PART_NAME_LEN];
} bootdata_partition_t;

typedef struct {
    uint64_t block_count;
    uint64_t block_size;
    // pdev_vid/pid/did are used to match partition map to
    // appropriate block device on the platform bus
    uint32_t pdev_vid;
    uint32_t pdev_pid;
    uint32_t pdev_did;
    uint32_t partition_count;
    char guid[BOOTDATA_PART_GUID_LEN];
    bootdata_partition_t partitions[];
} bootdata_partition_map_t;

typedef struct {
    uint64_t base;
    uint64_t length;
} bootdata_nvram_t;

#define BOOTDATA_UART_NONE 0
#define BOOTDATA_UART_PC_PORT 1
#define BOOTDATA_UART_PC_MMIO 2
typedef struct {
    uint64_t base;
    uint32_t type;
    uint32_t irq;
} bootdata_uart_t;

typedef struct {
    uint32_t vid;
    uint32_t pid;
    char board_name[32];
} bootdata_platform_id_t;

typedef struct {
    uint32_t cpu_count;     // number of CPU cores in the cluster
    uint32_t type;          // for future use
    uint32_t flags;         // for future use
    uint32_t reserved;
} bootdata_cpu_cluster_t;

typedef struct {
    uint32_t cluster_count;
    uint32_t reserved[3];
    bootdata_cpu_cluster_t clusters[];
} bootdata_cpu_config_t;

#define BOOTDATA_MEM_RANGE_RAM          1
#define BOOTDATA_MEM_RANGE_PERIPHERAL   2
#define BOOTDATA_MEM_RANGE_RESERVED     3
typedef struct {
    uint64_t    paddr;
    uint64_t    length;
    uint32_t    type;
    uint32_t    reserved;
} bootdata_mem_range_t;

/* EFI Variable for Crash Log */
#define ZIRCON_VENDOR_GUID \
    {0x82305eb2, 0xd39e, 0x4575, {0xa0, 0xc8, 0x6c, 0x20, 0x72, 0xd0, 0x84, 0x4c}}
#define ZIRCON_CRASHLOG_EFIVAR \
    { 'c', 'r', 'a', 's', 'h', 'l', 'o', 'g', 0 };
#define ZIRCON_CRASHLOG_EFIATTR \
    (EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS)

__END_CDECLS;


// BOOTFS is a trivial "filesystem" format
//
// It consists of a bootfs_header_t
//
// Followed by a series of bootfs_entry_t's of:
//   name length (32bit le)
//   data size   (32bit le)
//   data offset (32bit le)
//   namedata   (namelength bytes, includes \0)
//
// - data offsets must be page aligned (multiple of 4096)
// - entries start on uint32 boundaries

//lsw of sha256("bootfs")
#define BOOTFS_MAGIC (0xa56d3ff9)

#define BOOTFS_MAX_NAME_LEN 256

typedef struct bootfs_header {
    // magic value BOOTFS_MAGIC
    uint32_t magic;

    // total size of all bootfs_entry_t's
    // does not include the size of the bootfs_header_t
    uint32_t dirsize;

    // 0, 0
    uint32_t reserved0;
    uint32_t reserved1;
} bootfs_header_t;

typedef struct bootfs_entry {
    uint32_t name_len;
    uint32_t data_len;
    uint32_t data_off;
    char name[];
} bootfs_entry_t;

#define BOOTFS_ALIGN(nlen) (((nlen) + 3) & (~3))
#define BOOTFS_RECSIZE(entry) \
    (sizeof(bootfs_entry_t) + BOOTFS_ALIGN(entry->name_len))

#endif
