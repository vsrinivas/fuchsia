// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <inttypes.h>

#include <zircon/compiler.h>

__BEGIN_CDECLS

// Creates a ram_nand, returning the full path to the new device. The provided
// buffer for the path should be at least PATH_MAX characters long.
//
// Returns 0 on success.
int create_ram_nand(uint32_t page_size, uint32_t pages_per_block, uint32_t block_count,
                    uint32_t ecc_bits, char* out_path);

// Destroys a ram_nand, given the name returned from create_ram_nand().
//
// Returns 0 on success.
int destroy_ram_nand(const char* ram_nand_path);

__END_CDECLS
