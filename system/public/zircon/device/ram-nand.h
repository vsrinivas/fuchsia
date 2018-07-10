// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <limits.h>
#include <stdbool.h>

#include <zircon/boot/image.h>
#include <zircon/device/ioctl.h>
#include <zircon/device/ioctl-wrapper.h>
#include <zircon/device/nand.h>

#define IOCTL_RAM_NAND_CREATE \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_RAMDISK, 1)
#define IOCTL_RAM_NAND_UNLINK \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_RAMDISK, 2)
#define IOCTL_RAM_NAND_SET_BAD_BLOCKS \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_RAMDISK, 3)

#define RAM_NAND_PARTITION_MAX          5

// Describes extra partition information that is not described by the partition map.
typedef struct ram_nand_partition_config {
    uint8_t type_guid[ZBI_PARTITION_GUID_LEN];
    // The number of copies.
    uint32_t copy_count;
    // Offset each copy resides from each other.
    uint32_t copy_byte_offset;
} ram_nand_partition_config_t;

typedef struct ram_nand_info {
    nand_info_t nand_info;
    bool export_nand_config;
    bool export_partition_map;
    struct {
        // First block in which BBT may be be found.
        uint32_t table_start_block;
        // Last block in which BBT may be be found. It is inclusive.
        uint32_t table_end_block;
    } bad_block_config;
    uint32_t extra_partition_config_count;
    ram_nand_partition_config_t extra_partition_config[RAM_NAND_PARTITION_MAX];
    struct {
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

        // partition_count partition entries follow.
        zbi_partition_t partitions[RAM_NAND_PARTITION_MAX];
    } partition_map;
} ram_nand_info_t;

typedef struct ram_nand_name {
    char name[NAME_MAX + 1];
} ram_nand_name_t;

// ssize_t ioctl_ram_nand_config(int fd, const ram_nand_info_t* in,
//                               ram_nand_name_t* out);
// Must be issued to the control device.
IOCTL_WRAPPER_INOUT(ioctl_ram_nand_create, IOCTL_RAM_NAND_CREATE, ram_nand_info_t,
                    ram_nand_name_t);

// ssize_t ioctl_ram_nand_unlink(int fd);
IOCTL_WRAPPER(ioctl_ram_nand_unlink, IOCTL_RAM_NAND_UNLINK);

// ssize_t ioctl_ram_nand_set_bad_blocks(int fd, const uint32_t* bad_block_entries,
//                                       size_t table_size);
IOCTL_WRAPPER_VARIN(ioctl_ram_nand_set_bad_blocks, IOCTL_RAM_NAND_SET_BAD_BLOCKS,
                    uint32_t);
