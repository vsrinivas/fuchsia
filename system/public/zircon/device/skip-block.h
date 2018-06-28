// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <limits.h>

#include <zircon/boot/image.h>
#include <zircon/device/ioctl.h>
#include <zircon/device/ioctl-wrapper.h>
#include <zircon/types.h>

#define IOCTL_SKIP_BLOCK_GET_PARTITION_INFO \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_SKIP_BLOCK, 1)
#define IOCTL_SKIP_BLOCK_READ \
    IOCTL(IOCTL_KIND_SET_HANDLE, IOCTL_FAMILY_SKIP_BLOCK, 2)
#define IOCTL_SKIP_BLOCK_WRITE \
    IOCTL(IOCTL_KIND_SET_HANDLE, IOCTL_FAMILY_SKIP_BLOCK, 3)

typedef struct skip_block_partition_info {
    // Partition type GUID.
    uint8_t partition_guid[ZBI_PARTITION_GUID_LEN];
    // Describes the read/write size.
    size_t block_size_bytes;
    // Describes size of partition in terms of blocks.
    size_t partition_block_count;
} skip_block_partition_info_t;

typedef struct skip_block_rw_operation {
    // Memory object describing buffer to read into or write from.
    zx_handle_t vmo;
    // VMO offset in bytes.
    uint64_t vmo_offset;
    // Block # to begin operation from.
    uint32_t block;
    // Number of blocks to read or write.
    uint32_t block_count;
} skip_block_rw_operation_t;

// ssize_t ioctl_skip_block_get_partition_size(int fd,
//                                             skip_block_partition_info_t* partition_info_out);
//
// The block count can shrink in the event that a bad block is grown. It is
// recommended to call this again after a bad block is grown.
IOCTL_WRAPPER_OUT(ioctl_skip_block_get_partition_info, IOCTL_SKIP_BLOCK_GET_PARTITION_INFO,
                  skip_block_partition_info_t);

// ssize_t ioctl_skip_block_read(int fd, const skip_block_rw_operation_t in);
IOCTL_WRAPPER_IN(ioctl_skip_block_read, IOCTL_SKIP_BLOCK_READ, skip_block_rw_operation_t);

// ssize_t ioctl_skip_block_write(int fd, const skip_block_rw_operation_t in,
//                                bool* bad_block_grown);
//
// In the event that bad block is grown, the partition will shrink and
// |bad_block_grown| will be set to true. Since this causes the logical to
// physical block map to change, all previously written blocks at logical
// addresses after the section being written should be considered corrupted,
// and rewritten if applicable.
IOCTL_WRAPPER_INOUT(ioctl_skip_block_write, IOCTL_SKIP_BLOCK_WRITE, skip_block_rw_operation_t,
                    bool);

