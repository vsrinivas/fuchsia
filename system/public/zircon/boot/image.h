// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#ifndef __ASSEMBLER__
#include <stdint.h>
#endif

// Zircon Boot Image format (ZBI).
//
// A Zircon Boot Image consists of a container header followed by boot
// items.  Each boot item has a header (zbi_header_t) and then a payload of
// zbi_header_t.length bytes, which can be any size.  The zbi_header_t.type
// field indicates how to interpret the payload.  Many types specify an
// additional type-specific header that begins a variable-sized payload.
// zbi_header_t.length does not include the zbi_header_t itself, but does
// include any type-specific headers as part of the payload.  All fields in
// all header formats are little-endian.
//
// Padding bytes appear after each item as needed to align the payload size
// up to a ZBI_ALIGNMENT (8-byte) boundary.  This padding is not reflected
// in the zbi_header_t.length value.
//
// A "complete" ZBI can be booted by a Zircon-compatible boot loader.
// It contains one ZBI_TYPE_KERNEL_{ARCH} boot item that must come first,
// followed by any number of additional boot items, which must include
// exactly one ZBI_TYPE_STORAGE_BOOTFS item.
//
// A partial ZBI cannot be booted, and is only used during the build process.
// It contains one or more boot items and can be combined with other ZBIs to
// make a complete ZBI.

// All items begin at an 8-byte aligned offset into the image.
#ifdef __ASSEMBLER__
#define ZBI_ALIGNMENT           (8)
#else
#define ZBI_ALIGNMENT           (8u)
#endif

// Round n up to the next 8 byte boundary
#define ZBI_ALIGN(n)            (((n) + ZBI_ALIGNMENT - 1) & -ZBI_ALIGNMENT)

// LSW of sha256("bootdata")
#define ZBI_CONTAINER_MAGIC     (0x868cf7e6)

// LSW of sha256("bootitem")
#define ZBI_ITEM_MAGIC          (0xb5781729)

// This flag is always required.
#define ZBI_FLAG_VERSION        (0x00010000)

// ZBI items with the CRC32 flag must have a valid crc32.
// Otherwise their crc32 field must contain ZBI_ITEM_NO_CRC32
#define ZBI_FLAG_CRC32          (0x00020000)

// Value for zbi_header_t.crc32 when ZBI_FLAG_CRC32 is not set.
#define ZBI_ITEM_NO_CRC32       (0x4a87e8d6)

#ifndef __ASSEMBLER__
// Each header must be 8-byte aligned.  The length field specifies the
// actual payload length and does not include the size of padding.
typedef struct {
    // ZBI_TYPE_* constant, see below.
    uint32_t type;

    // Size of the payload immediately following this header.  This
    // does not include the header itself nor any alignment padding
    // after the payload.
    uint32_t length;

    // Type-specific extra data.  Each type specifies the use of this
    // field; see below.  When not explicitly specified, it should be zero.
    uint32_t extra;

    // Flags for this item.  This must always include ZBI_FLAG_VERSION.
    // It should contain ZBI_FLAG_CRC32 for any item where it's feasible
    // to compute the CRC32 at build time.  Other flags are specific to
    // each type; see below.
    uint32_t flags;

    // For future expansion.  Set to 0.
    uint32_t reserved0;
    uint32_t reserved1;

    // Must be ZBI_ITEM_MAGIC.
    uint32_t magic;

    // Must be the CRC32 of payload if ZBI_FLAG_CRC32 is set,
    // otherwise must be ZBI_ITEM_NO_CRC32.
    uint32_t crc32;
} zbi_header_t;
#endif

// Be sure to add new types to ZBI_ALL_TYPES.
#define ZBI_ALL_TYPES(macro) \
    macro(ZBI_TYPE_CONTAINER, "CONTAINER", ".bin") \
    macro(ZBI_TYPE_KERNEL_X64, "KERNEL_X64", ".bin") \
    macro(ZBI_TYPE_KERNEL_ARM64, "KERNEL_ARM64", ".bin") \
    macro(ZBI_TYPE_DISCARD, "DISCARD", ".bin") \
    macro(ZBI_TYPE_STORAGE_RAMDISK, "RAMDISK", ".bin") \
    macro(ZBI_TYPE_STORAGE_BOOTFS, "BOOTFS", ".bin") \
    macro(ZBI_TYPE_CMDLINE, "CMDLINE", ".txt") \
    macro(ZBI_TYPE_CRASHLOG, "CRASHLOG", ".bin") \
    macro(ZBI_TYPE_NVRAM, "NVRAM", ".bin") \
    macro(ZBI_TYPE_PLATFORM_ID, "PLATFORM_ID", ".bin") \
    macro(ZBI_TYPE_CPU_CONFIG, "CPU_CONFIG", ".bin") \
    macro(ZBI_TYPE_MEM_CONFIG, "MEM_CONFIG", ".bin") \
    macro(ZBI_TYPE_KERNEL_DRIVER, "KERNEL_DRIVER", ".bin") \
    macro(ZBI_TYPE_ACPI_RSDP, "ACPI_RSDP", ".bin") \
    macro(ZBI_TYPE_SMBIOS, "SMBIOS", ".bin") \
    macro(ZBI_TYPE_EFI_MEMORY_MAP, "EFI_MEMORY_MAP", ".bin") \
    macro(ZBI_TYPE_EFI_SYSTEM_TABLE, "EFI_SYSTEM_TABLE", ".bin") \
    macro(ZBI_TYPE_E820_TABLE, "E820_TABLE", ".bin") \
    macro(ZBI_TYPE_DEBUG_UART, "DEBUG_UART", ".bin") \
    macro(ZBI_TYPE_FRAMEBUFFER, "FRAMEBUFFER", ".bin") \
    macro(ZBI_TYPE_DRV_MAC_ADDRESS, "DRV_MAC_ADDRESS", ".bin") \
    macro(ZBI_TYPE_DRV_PARTITION_MAP, "DRV_PARTITION_MAP", ".bin") \
    macro(ZBI_TYPE_BOOT_CONFIG, "BOOT_CONFIG", ".bin") \
    macro(ZBI_TYPE_BOOT_VERSION, "BOOT_VERSION", ".bin")

// Each ZBI starts with a container header.
//     length:          Total size of the image after this header.
//                      This includes all item headers, payloads, and padding.
//                      It does not include the container header itself.
//                      Must be a multiple of ZBI_ALIGNMENT.
//     extra:           Must be ZBI_CONTAINER_MAGIC.
//     flags:           Must be ZBI_FLAG_VERSION and no other flags.
#define ZBI_TYPE_CONTAINER      (0x544f4f42) // BOOT

// Define a container header in assembly code.  The symbol name is defined
// as a local label; use .global symbol to make it global.  The length
// argument can use assembly label arithmetic like any immediate operand.
#ifdef __ASSEMBLER__
#define ZBI_CONTAINER_HEADER(symbol, length)    \
    .balign ZBI_ALIGNMENT;                      \
    symbol:                                     \
        .int ZBI_TYPE_CONTAINER;                \
        .int (length);                          \
        .int ZBI_CONTAINER_MAGIC;               \
        .int ZBI_FLAG_VERSION;                  \
        .int 0;                                 \
        .int 0;                                 \
        .int ZBI_ITEM_MAGIC;                    \
        .int ZBI_ITEM_NO_CRC32;                 \
    .size symbol, . - symbol;                   \
    .type symbol, %object
#else
#define ZBI_CONTAINER_HEADER(length) {          \
    ZBI_TYPE_CONTAINER,                         \
    (length),                                   \
    ZBI_CONTAINER_MAGIC,                        \
    ZBI_FLAG_VERSION,                           \
    0,                                          \
    0,                                          \
    ZBI_ITEM_MAGIC,                             \
    ZBI_ITEM_NO_CRC32,                          \
}
#endif


// The kernel image.  In a complete ZBI this item must always be first,
// immediately after the ZBI_TYPE_CONTAINER header.  The contiguous memory
// image of the kernel is formed from the ZBI_TYPE_CONTAINER header, the
// ZBI_TYPE_KERNEL_{ARCH} header, and the payload.
//
// The boot loader loads the whole image starting with the container header
// through to the end of the kernel item's payload into contiguous physical
// memory.  It then constructs a partial ZBI elsewhere in memory, which has
// a ZBI_TYPE_CONTAINER header of its own followed by all the other items
// that were in the booted ZBI plus other items synthesized by the boot
// loader to describe the machine.  This partial ZBI must be placed at an
// address (where the container header is found) that is aligned to the
// machine's page size.  The precise protocol for transferring control to
// the kernel's entry point varies by machine.
//
// On all machines, the kernel requires some amount of scratch memory to be
// available immediately after the kernel image at boot.  It needs this
// space for early setup work before it has a chance to read any memory-map
// information from the boot loader.  The `reserve_memory_size` field tells
// the boot loader how much space after the kernel's load image it must
// leave available for the kernel's use.  The boot loader must place its
// constructed ZBI or other reserved areas at least this many bytes after
// the kernel image.
//
// x86-64
//
//     The kernel assumes it was loaded at a fixed physical address of
//     0x100000 (1MB).  zbi_kernel_t.entry is the absolute physical address
//     of the PC location where the kernel will start.
//     TODO(SEC-31): Perhaps this will change??
//     The processor is in 64-bit mode with direct virtual to physical
//     mapping covering the physical memory where the kernel and
//     bootloader-constructed ZBI were loaded, which must be below 4GB.
//     The %rsi register (or %esi, since the high 32 bits must be zero)
//     holds the physical address of the bootloader-constructed ZBI.
//     All other registers are unspecified.
//
//  ARM64
//
//     zbi_kernel_t.entry is an offset from the beginning of the image
//     (i.e., the ZBI_TYPE_CONTAINER header before the ZBI_TYPE_KERNEL_ARM64
//     header) to the PC location in the image where the kernel will
//     start.  The processor is in physical address mode at EL1 or
//     above.  The kernel image and the bootloader-constructed ZBI each
//     can be loaded anywhere in physical memory.  The x0 register
//     holds the physical address of the bootloader-constructed ZBI.
//     All other registers are unspecified.
//
#define ZBI_TYPE_KERNEL_PREFIX     (0x004e524b) // KRN\0
#define ZBI_TYPE_KERNEL_MASK       (0x00FFFFFF)
#define ZBI_TYPE_KERNEL_X64        (0x4c4e524b) // KRNL
#define ZBI_TYPE_KERNEL_ARM64      (0x384e524b) // KRN8
#define ZBI_IS_KERNEL_BOOTITEM(x)  (((x) & ZBI_TYPE_KERNEL_MASK) ==  \
                                    ZBI_TYPE_KERNEL_PREFIX)

#ifndef __ASSEMBLER__
typedef struct {
    // Entry-point address.  The interpretation of this differs by machine.
    uint64_t entry;
    // Minimum amount (in bytes) of scratch memory that the kernel requires
    // immediately after its load image.
    uint64_t reserve_memory_size;
} zbi_kernel_t;

// The whole contiguous image loaded into memory by the boot loader.
typedef struct {
    zbi_header_t hdr_file;
    zbi_header_t hdr_kernel;
    zbi_kernel_t data_kernel;
    uint8_t contents[/*hdr_kernel.length - sizeof(zbi_kernel_t)*/];
    // data_kernel.reserve_memory_size bytes in memory are free after contents.
} zircon_kernel_t;
#endif


// A discarded item that should just be ignored.  This is used for an
// item that was already processed and should be ignored by whatever
// stage is now looking at the ZBI.  An earlier stage already "consumed"
// this information, but avoided copying data around to remove it from
// the ZBI item stream.
#define ZBI_TYPE_DISCARD        (0x50494b53) // SKIP


// ZBI_TYPE_STORAGE_* types represent an image that might otherwise
// appear on some block storage device, i.e. a RAM disk of some sort.
// All zbi_header_t fields have the same meanings for all these types.
// The interpretation of the payload (after possible decompression) is
// indicated by the specific zbi_header_t.type value.
//
// If ZBI_FLAG_STORAGE_COMPRESSED is set in zbi_header_t.flags, then the
// payload is compressed with LZ4 and zbi_header_t.extra gives the exact
// size of the decompressed payload.  If ZBI_FLAG_STORAGE_COMPRESSED is
// not set, then zbi_header_t.extra matches zbi_header_t.length.
//
// TODO(mcgrathr): Document or point to the details of the LZ4 header format.
#define ZBI_FLAG_STORAGE_COMPRESSED     (0x00000001)

// A virtual disk image.  This is meant to be treated as if it were a
// storage device.  The payload (after decompression) is the contents of
// the storage device, in whatever format that might be.
#define ZBI_TYPE_STORAGE_RAMDISK        (0x4b534452) // RDSK

// The /boot filesystem in BOOTFS format, specified below.
// A complete ZBI must have exactly one ZBI_TYPE_STORAGE_BOOTFS item.
// Zircon [userboot](../../../../docs/userboot.md) handles the contents
// of this filesystem.
#define ZBI_TYPE_STORAGE_BOOTFS         (0x42534642) // BFSB

// The payload (after decompression) of an item in BOOTFS format consists
// of separate "file" images that are each aligned to ZBI_BOOTFS_PAGE_SIZE
// bytes from the beginning of the item payload.  The first "file" consists
// of a zbi_bootfs_header_t followed by directory entries.
#define ZBI_BOOTFS_PAGE_SIZE            (4096u)

#define ZBI_BOOTFS_PAGE_ALIGN(size) \
    (((size) + ZBI_BOOTFS_PAGE_SIZE - 1) & -ZBI_BOOTFS_PAGE_SIZE)

#ifndef __ASSEMBLER__
typedef struct {
    // Must be ZBI_BOOTFS_MAGIC.
    uint32_t magic;

    // Size in bytes of all the directory entries.
    // Does not include the size of the zbi_bootfs_header_t.
    uint32_t dirsize;

    // Reserved for future use.  Set to 0.
    uint32_t reserved0;
    uint32_t reserved1;
} zbi_bootfs_header_t;
#endif

// LSW of sha256("bootfs")
#define ZBI_BOOTFS_MAGIC                (0xa56d3ff9)

// Each directory entry holds a pathname and gives the offset and size
// of the contents of the file by that name.
#ifndef __ASSEMBLER__
typedef struct {
    // Length of the name[] field at the end.  This length includes the
    // NUL terminator, which must be present, but does not include any
    // alignment padding required before the next directory entry.
    uint32_t name_len;

    // Length of the file in bytes.  This is an exact size that is not
    // rounded, though the file is always padded with zeros up to a
    // multiple of ZBI_BOOTFS_PAGE_SIZE.
    uint32_t data_len;

    // Offset from the beginning of the payload (zbi_bootfs_header_t) to
    // the file's data.  This must be a multiple of ZBI_BOOTFS_PAGE_SIZE.
    uint32_t data_off;

    // Pathname of the file, a UTF-8 string.  This must include a NUL
    // terminator at the end.  It must not begin with a '/', but it may
    // contain '/' separators for subdirectories.
    char name[];
} zbi_bootfs_dirent_t;
#endif

// Each directory entry has a variable size of [16,268] bytes that
// must be a multiple of 4 bytes.
#define ZBI_BOOTFS_DIRENT_SIZE(name_len) \
    ((sizeof(zbi_bootfs_dirent_t) + (name_len) + 3) & -(size_t)4)

// zbi_bootfs_dirent_t.name_len must be > 1 and <= ZBI_BOOTFS_MAX_NAME_LEN.
#define ZBI_BOOTFS_MAX_NAME_LEN         (256)


// The remaining types are used to communicate information from the boot
// loader to the kernel.  Usually these are synthesized in memory by the
// boot loader, but they can also be included in a ZBI along with the
// kernel and BOOTFS.  Some boot loaders may set the zbi_header_t flags
// and crc32 fields to zero, though setting them to ZBI_FLAG_VERSION and
// ZBI_ITEM_NO_CRC32 is specified.  The kernel doesn't check.


// A kernel command line fragment, a NUL-terminated UTF-8 string.
// Multiple ZBI_TYPE_CMDLINE items can appear.  They are treated as if
// concatented with ' ' between each item, in the order they appear:
// first items in the complete ZBI containing the kernel; then items in
// the ZBI synthesized by the boot loader.  The kernel interprets the
// [whole command line](../../../../docs/kernel_cmdline.md).
#define ZBI_TYPE_CMDLINE                (0x4c444d43) // CMDL

// The crash log from the previous boot, a UTF-8 string.
#define ZBI_TYPE_CRASHLOG               (0x4d4f4f42) // BOOM

// Physical memory region that will persist across warm boots.
// zbi_nvram_t gives the physical base address and length in bytes.
#define ZBI_TYPE_NVRAM                  (0x4c4c564e) // NVLL
// This reflects a typo we need to support for a while.
#define ZBI_TYPE_NVRAM_DEPRECATED       (0x4c4c5643) // CVLL
#ifndef __ASSEMBLER__
typedef struct {
    uint64_t base;
    uint64_t length;
} zbi_nvram_t;
#endif

#define ZBI_BOARD_NAME_LEN 32

// Platform ID Information.
#define ZBI_TYPE_PLATFORM_ID            (0x44494C50) // PLID
#ifndef __ASSEMBLER__
typedef struct {
    uint32_t vid;
    uint32_t pid;
    char board_name[ZBI_BOARD_NAME_LEN];
} zbi_platform_id_t;
#endif

// CPU configuration, a zbi_cpu_config_t header followed by one or more
// zbi_cpu_cluster_t entries.  zbi_header_t.length must equal
// zbi_cpu_config_t.cluster_count * sizeof(zbi_cpu_cluster_t).
#define ZBI_TYPE_CPU_CONFIG             (0x43555043) // CPUC
#ifndef __ASSEMBLER__
typedef struct {
    // Number of CPU cores in the cluster.
    uint32_t cpu_count;

    // Reserved for future use.  Set to 0.
    uint32_t type;
    uint32_t flags;
    uint32_t reserved;
} zbi_cpu_cluster_t;

typedef struct {
    // Number of zbi_cpu_cluster_t entries following this header.
    uint32_t cluster_count;

    // Reserved for future use.  Set to 0.
    uint32_t reserved[3];

    // cluster_count entries follow.
    zbi_cpu_cluster_t clusters[];
} zbi_cpu_config_t;
#endif

// Memory configuration, one or more zbi_mem_range_t entries.
// zbi_header_t.length is sizeof(zbi_mem_range_t) times the number of entries.
#define ZBI_TYPE_MEM_CONFIG             (0x434D454D) // MEMC
#ifndef __ASSEMBLER__
typedef struct {
    uint64_t    paddr;
    uint64_t    length;
    uint32_t    type;
    uint32_t    reserved;
} zbi_mem_range_t;
#endif
#define ZBI_MEM_RANGE_RAM               (1)
#define ZBI_MEM_RANGE_PERIPHERAL        (2)
#define ZBI_MEM_RANGE_RESERVED          (3)

// Kernel driver configuration.  The zbi_header_t.extra field gives a
// KDRV_* type that determines the payload format.
// See [driver-config.h](<zircon/boot/driver-config.h>) for details.
#define ZBI_TYPE_KERNEL_DRIVER          (0x5652444B) // KDRV

// ACPI Root Table Pointer, a uint64_t physical address.
#define ZBI_TYPE_ACPI_RSDP              (0x50445352) // RSDP

// SMBIOS entry point, a uint64_t physical address.
#define ZBI_TYPE_SMBIOS                 (0x49424d53) // SMBI

// EFI memory map, a uint64_t entry size followed by a sequence of
// EFI memory descriptors aligned on that entry size.
#define ZBI_TYPE_EFI_MEMORY_MAP         (0x4d494645) // EFIM

// EFI system table, a uint64_t physical address.
#define ZBI_TYPE_EFI_SYSTEM_TABLE       (0x53494645) // EFIS

// E820 memory table, an array of e820entry_t.
#define ZBI_TYPE_E820_TABLE             (0x30323845) // E820

/* EFI Variable for Crash Log */
#define ZIRCON_VENDOR_GUID \
    {0x82305eb2, 0xd39e, 0x4575, {0xa0, 0xc8, 0x6c, 0x20, 0x72, 0xd0, 0x84, 0x4c}}
#define ZIRCON_CRASHLOG_EFIVAR \
    { 'c', 'r', 'a', 's', 'h', 'l', 'o', 'g', 0 };
#define ZIRCON_CRASHLOG_EFIATTR \
    (EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS)

// Debug serial port, a zbi_uart_t entry.
#define ZBI_TYPE_DEBUG_UART             (0x54524155) // UART
#ifndef __ASSEMBLER__
typedef struct {
    uint64_t base;
    uint32_t type;
    uint32_t irq;
} zbi_uart_t;
#endif
#define ZBI_UART_NONE                   (0)
#define ZBI_UART_PC_PORT                (1)
#define ZBI_UART_PC_MMIO                (2)

// Framebuffer parameters, a zbi_swfb_t entry.
#define ZBI_TYPE_FRAMEBUFFER            (0x42465753) // SWFB

// A copy of the boot configuration stored as a kvstore
// within the sysconfig partition.
#define ZBI_TYPE_BOOT_CONFIG        (0x47464342) // BCFG

// A copy of the boot version stored within the sysconfig
// partition
#define ZBI_TYPE_BOOT_VERSION       (0x53525642) // BVRS

#ifndef __ASSEMBLER__
typedef struct {
    // Physical memory address.
    uint64_t base;

    // Pixel layout and format.
    // See [../pixelformat.h](<zircon/pixelformat.h>).
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t format;
} zbi_swfb_t;
#endif


// ZBI_TYPE_DRV_* types (LSB is 'm') contain driver metadata.
#define ZBI_TYPE_DRV_METADATA(type)     (((type) & 0xFF) == 0x6D) // 'm'

// MAC address for Ethernet, Wifi, Bluetooth, etc.  zbi_header_t.extra
// is a board-specific index to specify which device the MAC address
// applies to.  zbi_header_t.length gives the size in bytes, which
// varies depending on the type of address appropriate for the device.
#define ZBI_TYPE_DRV_MAC_ADDRESS        (0x43414D6D) // mMAC

// A partition map for a storage device, a zbi_partition_map_t header
// followed by one or more zbi_partition_t entries.  zbi_header_t.extra
// is a board-specific index to specify which device this applies to.
#define ZBI_TYPE_DRV_PARTITION_MAP      (0x5452506D) // mPRT
#define ZBI_PARTITION_NAME_LEN          (32)
#define ZBI_PARTITION_GUID_LEN          (16)
#ifndef __ASSEMBLER__
typedef struct {
    // GUID specifying the format and use of data stored in the partition.
    uint8_t type_guid[ZBI_PARTITION_GUID_LEN];

    // GUID unique to this partition.
    uint8_t uniq_guid[ZBI_PARTITION_GUID_LEN];

    // First and last block occupied by this partition.
    uint64_t first_block;
    uint64_t last_block;

    // Reserved for future use.  Set to 0.
    uint64_t flags;

    char name[ZBI_PARTITION_NAME_LEN];
} zbi_partition_t;

typedef struct {
    // Total blocks used on the device.
    uint64_t block_count;
    // Size of each block in bytes.
    uint64_t block_size;

    // Number of partitions in the map.
    uint32_t partition_count;

    // Reserved for future use.
    uint32_t reserved;

    // Device GUID.
    uint8_t guid[ZBI_PARTITION_GUID_LEN];

    // parition_count partition entries follow.
    zbi_partition_t partitions[];
} zbi_partition_map_t;
#endif
