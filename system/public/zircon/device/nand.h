// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/boot/image.h>

enum {
    NAND_CLASS_PARTMAP = 1,   // NAND device contains multiple partitions.
    NAND_CLASS_FTL = 2,       // NAND device is a FTL partition.
    NAND_CLASS_BBS = 3,       // NAND device is a bad block skip partition.
};

// nand_info_t is used to retrieve various parameters describing the geometry of
// the underlying NAND chip(s). This is retrieved using the query api in
// nand_protocol_ops.
typedef struct nand_info nand_info_t;

struct nand_info {
    uint32_t page_size;         // Read/write unit size, in bytes.
    uint32_t pages_per_block;   // Erase block size, in pages.
    uint32_t num_blocks;        // Device size, in erase blocks.
    uint32_t ecc_bits;          // Number of ECC bits (correctable bit flips),
                                // per correction chunk.
    uint32_t oob_size;          // Available out of band bytes per page.
    uint32_t nand_class;        // NAND_CLASS_PARTMAP, NAND_CLASS_FTL or NAND_CLASS_RAW.
    uint8_t partition_guid[ZBI_PARTITION_GUID_LEN]; // partition type GUID from partition map.
};
