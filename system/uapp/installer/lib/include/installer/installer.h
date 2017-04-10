// Copyright 2017 The Fuchsia Authors. All rights reserved.
// User of this source code is governed by a BSD-style license that be be found
// in the LICENSE file.

#pragma once

#include <gpt/gpt.h>
#include <magenta/syscalls.h>

// the first and last 17K of the disk using GPT is reserved for
// 512B for the MBR, 512B the GPT header, and 16K for for 128, 128B
// partition entries. Technically the reserved space is two blocks
// plus 16KB, so we're making an assumption here that block sizes are
// 512B.
#define SIZE_RESERVED (((uint32_t)17) * 1024)

typedef struct part_tuple {
  int index;
  size_t first;
} part_tuple_t;

typedef struct part_location {
  size_t blk_offset;
  size_t blk_len;
} part_location_t;

mx_status_t find_partition_entries(gpt_partition_t **gpt_table,
                                   const uint8_t (*guid)[GPT_GUID_LEN],
                                   uint16_t table_size,
                                   uint16_t *part_id_out);

mx_status_t find_partition(gpt_partition_t **gpt_table,
                           const uint8_t (*part_guid)[GPT_GUID_LEN],
                           uint64_t min_size, uint64_t block_size,
                           const char *part_name, uint16_t table_size,
                           uint16_t *part_index_out,
                           gpt_partition_t **part_info_out);

bool check_partition_size(const gpt_partition_t *partition,
                          const uint64_t min_size, const uint64_t block_size,
                          const char *partition_name);

gpt_partition_t **sort_partitions(gpt_partition_t **parts, uint16_t count);

void find_available_space(gpt_device_t *device, size_t blocks_req,
                          size_t block_count, size_t block_size,
                          part_location_t *result_out);
