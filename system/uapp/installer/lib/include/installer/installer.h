// Copyright 2017 The Fuchsia Authors. All rights reserved.
// User of this source code is governed by a BSD-style license that be be found
// in the LICENSE file.

#pragma once

#include <dirent.h>
#include <gpt/gpt.h>
#include <zircon/syscalls.h>

// the first and last 17K of the disk using GPT is reserved for
// 512B for the MBR, 512B the GPT header, and 16K for for 128, 128B
// partition entries. Technically the reserved space is two blocks
// plus 16KB, so we're making an assumption here that block sizes are
// 512B.
#define SIZE_RESERVED (((uint32_t)17) * 1024)
#define PATH_BLOCKDEVS "/dev/class/block"

typedef struct part_tuple {
  int index;
  size_t first;
} part_tuple_t;

typedef struct part_location {
  size_t blk_offset;
  size_t blk_len;
} part_location_t;

zx_status_t find_partition_entries(gpt_partition_t **gpt_table,
                                   const uint8_t (*guid)[GPT_GUID_LEN],
                                   uint16_t table_size,
                                   uint16_t *part_id_out);

zx_status_t find_partition(gpt_partition_t **gpt_table,
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
/*
 * Given a directory, dir, which contains a group of devices, examine each
 * device to determine if any has a GPT whose header GUID matches the supplied
 * disk_guid. If successful we return ZX_OK. install_dev_out will be set to
 * point to a gpt_device_t which has been opened read-only and whose underlying
 * fd is closed. To modify the device's GPT, use disk_path_out to open the GPT
 * read/write.
 */
zx_status_t find_disk_by_guid(DIR *dir, const char* dir_path,
                              uint8_t (*disk_guid)[GPT_GUID_LEN],
                              gpt_device_t **install_dev_out,
                              char *disk_path_out, ssize_t max_len);

/*
 * Read the next entry out of the directory and place copy it into name_out.
 *
 * Return the difference between the length of the name and the number of bytes
 * we did or would be able to copy.
 *  * -1 if there are no more entries
 *  * 0 if an entry is successfully read and all its bytes were copied into
 *      name_out
 *  * A positive value, which is the amount by which copying the directory name
 *      would exceed the limit expressed by max_name_len.
 */
ssize_t get_next_file_path(DIR *dfd, size_t max_name_len, char *name_out);

int open_device_ro(const char *dev_path);

gpt_device_t *read_gpt(int fd, uint64_t *blocksize_out);
