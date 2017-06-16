// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#define PARTITIONS_COUNT 128
#define GPT_GUID_LEN 16
#define GPT_GUID_STRLEN 37
#define GPT_NAME_LEN 72

#define GUID_EFI_VALUE {                           \
    0x28, 0x73, 0x2a, 0xc1,                        \
    0x1f, 0xf8,                                    \
    0xd2, 0x11,                                    \
    0xba, 0x4b, 0x00, 0xa0, 0xc9, 0x3e, 0xc9, 0x3b \
}

// GUID for a system partition
#define GUID_SYSTEM_STRING "606B000B-B7C7-4653-A7D5-B737332C899D"
#define GUID_SYSTEM_VALUE {                        \
    0x0b, 0x00, 0x6b, 0x60,                        \
    0xc7, 0xb7,                                    \
    0x53, 0x46,                                    \
    0xa7, 0xd5, 0xb7, 0x37, 0x33, 0x2c, 0x89, 0x9d \
}

// GUID for a data partition
#define GUID_DATA_STRING "08185F0C-892D-428A-A789-DBEEC8F55E6A"
#define GUID_DATA_VALUE {                          \
    0x0c, 0x5f, 0x18, 0x08,                        \
    0x2d, 0x89,                                    \
    0x8a, 0x42,                                    \
    0xa7, 0x89, 0xdb, 0xee, 0xc8, 0xf5, 0x5e, 0x6a \
}

#define GUID_BLOBFS_STRING "2967380E-134C-4CBB-B6DA-17E7CE1CA45D"
#define GUID_BLOBFS_VALUE {                        \
    0x0e, 0x38, 0x67, 0x29,                        \
    0x4c, 0x13,                                    \
    0xbb, 0x4c,                                    \
    0xb6, 0xda, 0x17, 0xe7, 0xce, 0x1c, 0xa4, 0x5d \
}

typedef struct gpt_partition {
    uint8_t type[GPT_GUID_LEN];
    uint8_t guid[GPT_GUID_LEN];
    uint64_t first;
    uint64_t last;
    uint64_t flags;
    uint8_t name[GPT_NAME_LEN];
} gpt_partition_t;

typedef struct gpt_device {
    bool valid;
    // true if the partition table on the device is valid
    gpt_partition_t* partitions[PARTITIONS_COUNT];
    // pointer to a list of partitions
} gpt_device_t;

int gpt_device_init(int fd, uint64_t blocksize, uint64_t blocks, gpt_device_t** out_dev);
// read the partition table from the device.

void gpt_device_release(gpt_device_t* dev);
// releases the device

int gpt_device_range(gpt_device_t* dev, uint64_t* block_start, uint64_t* block_end);
// Returns the range of usable blocks within the GPT, from [block_start, block_end] (inclusive)

int gpt_device_sync(gpt_device_t* dev);
// writes the partition table to the device. it is the caller's responsibility to
// rescan partitions for the block device if needed

int gpt_partition_add(gpt_device_t* dev, const char* name, uint8_t* type, uint8_t* guid,
                      uint64_t offset, uint64_t blocks, uint64_t flags);
// adds a partition

int gpt_partition_remove(gpt_device_t* dev, const uint8_t* guid);
// removes a partition

int gpt_partition_remove_all(gpt_device_t* dev);
// removes all partitions

void uint8_to_guid_string(char* dst, const uint8_t* src);
// converts GUID to a string

void gpt_device_get_header_guid(gpt_device_t* dev,
                                uint8_t (*disk_guid_out)[GPT_GUID_LEN]);
// given a gpt device, get the GUID for the disk
