// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/metadata/bad-block.h>

#include <zircon/boot/image.h>
#include <zircon/types.h>

#define NAND_PARTITION_MAX          10

// Describes extra partition information that is not described by the partition map.
typedef struct nand_partition_config {
    uint8_t type_guid[ZBI_PARTITION_GUID_LEN];
    // The number of copies.
    uint32_t copy_count;
    // Offset each copy resides from each other.
    uint32_t copy_byte_offset;
} nand_partition_config_t;

typedef struct nand_config {
    bad_block_config_t bad_block_config;
    uint32_t extra_partition_config_count;
    nand_partition_config_t extra_partition_config[NAND_PARTITION_MAX];
} nand_config_t;
