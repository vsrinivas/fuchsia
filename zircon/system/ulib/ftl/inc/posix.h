// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

#include <zircon/compiler.h>

// Types of Flash.
#define FFS_NAND_SLC (1 << 0)
#define FFS_NAND_MLC (1 << 1)

// Driver count statistics for TargetFTL-NDM volumes.
typedef struct {
  uint32_t write_page;
  uint32_t read_page;
  uint32_t read_spare;
  uint32_t page_check;
  uint32_t page_erased;
  uint32_t transfer_page;
  uint32_t erase_block;
  uint32_t ram_used;
  uint32_t wear_count;
} ftl_ndm_stats;

typedef struct {
  uint32_t num_blocks;

  // Percentage of space that is dirty from the total available. [0, 100).
  // Calculated as 100 x (1 - free_pages / volume_size - used_pages).
  uint32_t garbage_level;

  // Histogram of the wear level distribution. Each bucket represents about 5%
  // of the valid range, with the first bucket storing the number of blocks
  // with the lowest wear count, and the last bucket the most reused blocks.
  // If all blocks have the same wear count, the first 19 buckets will have no
  // samples.
  uint32_t wear_histogram[20];
  ftl_ndm_stats ndm;
} vstat;

__BEGIN_CDECLS

int FtlNdmDelVol(const char* name);

__END_CDECLS
