// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <limits.h>

#include <zircon/device/ioctl.h>
#include <zircon/device/ioctl-wrapper.h>
#include <zircon/device/nand.h>

#define IOCTL_RAM_NAND_CREATE \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_RAMDISK, 1)
#define IOCTL_RAM_NAND_UNLINK \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_RAMDISK, 2)
#define IOCTL_RAM_NAND_SET_BAD_BLOCKS \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_RAMDISK, 3)

typedef struct ram_nand_name {
    char name[NAME_MAX + 1];
} ram_nand_name_t;

// ssize_t ioctl_ram_nand_config(int fd, const ram_nand_ioctl_config_t* in,
//                               ram_nand_ioctl_config_response_t* out);
// Must be issued to the control device.
IOCTL_WRAPPER_INOUT(ioctl_ram_nand_create, IOCTL_RAM_NAND_CREATE, nand_info_t,
                    ram_nand_name_t);

// ssize_t ioctl_ram_nand_unlink(int fd);
IOCTL_WRAPPER(ioctl_ram_nand_unlink, IOCTL_RAM_NAND_UNLINK);

// ssize_t ioctl_ram_nand_set_bad_blocks(int fd, const uint32_t* bad_block_entries,
//                                       size_t table_size);
IOCTL_WRAPPER_VARIN(ioctl_ram_nand_set_bad_blocks, IOCTL_RAM_NAND_SET_BAD_BLOCKS,
                    uint32_t);
