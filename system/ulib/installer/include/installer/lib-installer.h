// Copyright 2017 The Fuchsia Authors. All rights reserved.
// User of this source code is governed by a BSD-style license that be be found
// in the LICENSE file.

#include <gpt/gpt.h>
#include <magenta/syscalls.h>

mx_status_t find_partition_entries(gpt_partition_t** gpt_table,
                                   const uint8_t* guid,
                                   uint16_t table_size,
                                   uint16_t* part_id_out);

mx_status_t find_partition(gpt_partition_t** gpt_table,
                           const uint8_t* part_guid, uint64_t min_size,
                           uint64_t block_size, const char* part_name,
                           uint16_t table_size, uint16_t* part_index_out,
                           gpt_partition_t** part_info_out);

bool check_partition_size(const gpt_partition_t* partition,
                          const uint64_t min_size,
                          const uint64_t block_size,
                          const char* partition_name);
