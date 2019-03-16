// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DDK_METADATA_BAD_BLOCK_H_
#define DDK_METADATA_BAD_BLOCK_H_

#include <zircon/types.h>

// Enumeration of all different types of bad blocks.
typedef uint8_t bad_block_type_t;
#define kAmlogicUboot ((bad_block_type_t)0)

typedef struct {
    bad_block_type_t type;
    union {
        struct {
            // First block in which BBT may be be found.
            uint32_t table_start_block;
            // Last block in which BBT may be be found. It is inclusive.
            uint32_t table_end_block;
        } aml;
    };
} bad_block_config_t;

#endif  // DDK_METADATA_BAD_BLOCK_H_
