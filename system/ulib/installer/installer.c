// Copyright 2017 The Fuchsia Authors. All rights reserved.
// User of this source code is governed by a BSD-style license that be be found
// in the LICENSE file.

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <zircon/device/block.h>
#include <zircon/syscalls.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "include/installer/installer.h"

/*
 * Given a pointer to an array of gpt_partition_t structs, look for a partition
 * with the matching type GUID. The table_size argument tells this function how
 * many entries there are in the array.
 * Returns
 *   * ZX_ERR_INVALID_ARGS if any pointers are null
 *   * ZX_ERR_BAD_STATE if the GPT data reports itself as invalid
 *   * ZX_ERR_NOT_FOUND if the requested partition is not present
 *   * ZX_OK if the partition is found, in which case the index is assigned
 *     to part_id_out
 */
zx_status_t find_partition_entries(gpt_partition_t **gpt_table,
                                   const uint8_t (*guid)[GPT_GUID_LEN],
                                   uint16_t table_size, uint16_t *part_id_out) {
  assert(gpt_table != NULL);
  assert(guid != NULL);
  assert(part_id_out != NULL);

  for (uint16_t idx = 0; idx < table_size && gpt_table[idx] != NULL; idx++) {
    uint8_t *type_ptr = gpt_table[idx]->type;
    if (!memcmp(type_ptr, guid, 16)) {
      *part_id_out = idx;
      return ZX_OK;
    }
  }

  return ZX_ERR_NOT_FOUND;
}

/*
 * For the given partition, see if it is at least as large as min_size.
 */
bool check_partition_size(const gpt_partition_t *partition,
                          const uint64_t min_size, const uint64_t block_size,
                          const char *partition_name) {
  assert(partition != NULL);
  assert(partition->last >= partition->first);
  assert(partition->name != NULL);

  uint64_t block_count = partition->last - partition->first + 1;
  uint64_t partition_size =
      block_size * (partition->last - partition->first + 1);
  printf("%s has %" PRIu64 " blocks and block size of %" PRIu64 "\n",
         partition_name, block_count, block_size);

  if (partition_size < min_size) {
    fprintf(stderr, "%s partition too small, found %" PRIu64 ", but require \
                %" PRIu64 "\n",
            partition_name, partition_size, min_size);
    return false;
  } else {
    return true;
  }
}

/*
 * Given an array of GPT partition entries in gpt_table and a partition guid in
 * part_guid, validate that the partition is in the array. Further, validate
 * that the entry reports that the number of blocks in the partition multiplied
 * by the provided block_size meets the min_size. If more than one partition in
 * the array passes this test, the first match will be provided. The length of
 * the partition information array should be passed in table_size.
 *
 * If ZX_OK is returned, part_index_out will contain the index in the GPT for
 * the requested partition. Additionally, the pointer at *part_info_out will
 * point at the gpt_partition_t with information for the requested partition.
 */
zx_status_t find_partition(gpt_partition_t **gpt_table,
                           const uint8_t (*part_guid)[GPT_GUID_LEN],
                           uint64_t min_size, uint64_t block_size,
                           const char *part_name, uint16_t table_size,
                           uint16_t *part_index_out,
                           gpt_partition_t **part_info_out) {
  // if we find a partition, but it is the wrong size, we want to keep
  // looking down the table for something that might match, this offset
  // tracks our overall progress in the table
  uint16_t part_offset = 0;

  zx_status_t rc = ZX_OK;
  while (rc == ZX_OK) {
    // reduce table_size by the number of entries we've examined
    rc = find_partition_entries(gpt_table, part_guid, table_size - part_offset,
                                part_index_out);

    gpt_partition_t *partition;
    switch (rc) {
    case ZX_ERR_NOT_FOUND:
      fprintf(stderr, "No %s partition found.\n", part_name);
      break;
    case ZX_ERR_INVALID_ARGS:
      fprintf(stderr, "Arguments are invalid for %s partition.\n", part_name);
      break;
    case ZX_ERR_BAD_STATE:
      printf("GPT descriptor is invalid.\n");
      break;
    case ZX_OK:
      partition = gpt_table[*part_index_out];
      assert(partition->last >= partition->first);

      if (check_partition_size(partition, min_size, block_size, part_name)) {
        *part_info_out = partition;
        // adjust the output index by part_offset
        *part_index_out = part_offset + *part_index_out;
        return ZX_OK;
      } else {
        // if the size doesn't check out, keep looking for
        // partitions later in the table
        uint16_t jump_size = *part_index_out + 1;

        // advance the pointer
        gpt_table += jump_size;
        part_offset += jump_size;
      }
      break;
    default:
      fprintf(stderr, "Unrecognized error finding efi parition: %d\n", rc);
      break;
    }
  }

  // we didn't find a suitable partition
  return ZX_ERR_NOT_FOUND;
}

static int compare(const void *ls, const void *rs) {
  return (int)(((part_tuple_t *)ls)->first - ((part_tuple_t *)rs)->first);
}

/*
 * Sort an array of gpt_partition_t pointers based on the values of
 * gpt_partition_t->first. The returned value will contain an array of pointers
 * to partitions in sorted order. This array was allocated on the heap and
 * should be freed at some point.
 */
gpt_partition_t **sort_partitions(gpt_partition_t **parts, uint16_t count) {
  gpt_partition_t **sorted_parts = malloc(count * sizeof(gpt_partition_t *));
  if (sorted_parts == NULL) {
    fprintf(stderr, "Unable to sort partitions, out of memory.\n");
    return NULL;
  }

  part_tuple_t *sort_tuples = malloc(count * sizeof(part_tuple_t));
  if (sort_tuples == NULL) {
    fprintf(stderr, "Unable to sort partitions, out of memory.\n");
    free(sorted_parts);
    return NULL;
  }

  for (uint16_t idx = 0; idx < count; idx++, sort_tuples++) {
    sort_tuples->index = idx;
    sort_tuples->first = parts[idx]->first;
  }

  sort_tuples -= count;
  qsort(sort_tuples, count, sizeof(part_tuple_t), compare);

  // create a sorted array of pointers
  for (uint16_t idx = 0; idx < count; idx++, sort_tuples++) {
    sorted_parts[idx] = parts[sort_tuples->index];
  }

  sort_tuples -= count;
  free(sort_tuples);
  return sorted_parts;
}

/*
 * Attempt to find an unallocated portion of the specified device that is at
 * least blocks_req in size. block_count should contain the total number of
 * blocks on the disk. The offset in blocks of the allocation hole will be
 * returned or 0 if no space is available. If there is available space, but
 * no region is as large as requested, the next largest unallocated region
 * will be returned. Callers should check the result_out to see what is found.
 */
void find_available_space(gpt_device_t *device, size_t blocks_req,
                          size_t block_count, size_t block_size,
                          part_location_t *result_out) {
  assert(result_out != NULL);
  assert(device != NULL);

  gpt_partition_t **sorted_parts;
  // 17K is reserved at the front and back of the disk for the protected MBR
  // and the GPT. The front is the primary of these and the back is the backup
  const uint32_t blocks_resrvd = SIZE_RESERVED / block_size;
  result_out->blk_offset = 0;
  result_out->blk_len = 0;

  // if the device has no partitions, we can add one after the reserved
  // section at the front
  if (device->partitions[0] == NULL) {
    result_out->blk_len = block_count - blocks_resrvd * 2;
    result_out->blk_offset = blocks_resrvd;
    return;
  }

  // check if the GPT is sorted by partition position
  bool sorted = true;
  size_t count = 0;
  for (count = 1;
       count < PARTITIONS_COUNT && device->partitions[count] != NULL && sorted;
       count++) {
    sorted =
        device->partitions[count - 1]->first < device->partitions[count]->first;
  }

  if (!sorted) {
    // count the number of valid partitions because in this case we would
    // have bailed out of counting early
    for (count = 0; device->partitions[count] != NULL; count++)
      ;
    sorted_parts = sort_partitions(device->partitions, count);
    if (sorted_parts == NULL) {
      printf("Sort failure\n");
      return;
    }
  } else {
    sorted_parts = device->partitions;
  }

  // check to see if we have space at the beginning of the disk
  size_t gap = sorted_parts[0]->first - blocks_resrvd;
  if (result_out->blk_len < gap) {
    result_out->blk_offset = blocks_resrvd;
    result_out->blk_len = gap;
  }

  if (result_out->blk_len >= blocks_req) {
    if (!sorted) {
      free(sorted_parts);
    }
    return;
  }

  // check if there is space between partitions
  for (size_t idx = 1; idx < count; idx++) {
    gap = sorted_parts[idx]->first - sorted_parts[idx - 1]->last - 1;
    if (result_out->blk_len < gap) {
      result_out->blk_offset = sorted_parts[idx - 1]->last + 1;
      result_out->blk_len = gap;
    } else {
      continue;
    }

    if (result_out->blk_len >= blocks_req) {
      result_out->blk_offset = sorted_parts[idx - 1]->last + 1;
      if (!sorted) {
        free(sorted_parts);
      }
      return;
    }
  }

  if (sorted_parts[count - 1]->last > block_count) {
    printf("WARNING: last partition extends beyond end of disk.\n");
  }

  if (sorted_parts[count - 1]->last + blocks_resrvd + 1 >= block_count) {
    gap = 0;
  } else {
    gap = block_count - sorted_parts[count - 1]->last - blocks_resrvd - 1;
  }

  if (result_out->blk_len < gap) {
    result_out->blk_len = gap;
    result_out->blk_offset = sorted_parts[count - 1]->last + 1;
    if (!sorted) {
      free(sorted_parts);
    }
  }

  if (!sorted) {
    free(sorted_parts);
  }

  return;
}

ssize_t get_next_file_path(DIR *dfd, size_t max_name_len, char *name_out) {
  assert(name_out != NULL);
  struct dirent *entry;

  // skip and '.' or '..' entries
  while ((entry = readdir(dfd)) != NULL &&
         (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")))
    ;

  if (entry == NULL) {
    return -1;
  } else {
    size_t len_d_name = strlen(entry->d_name);
    ssize_t overrun = len_d_name - max_name_len + 1;
    if (overrun > 0) {
      assert(overrun <= SSIZE_MAX);
      return overrun;
    } else {
      // copy the string, including the null terminator
      memcpy(name_out, entry->d_name, len_d_name + 1);
      return 0;
    }
  }
}

/*
 * Attempt to read a GPT from the file descriptor. If successful the returned
 * pointer points to the populated gpt_device_t struct, otherwise NULL is
 * returned.
 */
gpt_device_t *read_gpt(int fd, uint64_t *blocksize_out) {
  assert(blocksize_out != NULL);
  block_info_t info;
  ssize_t rc = ioctl_block_get_info(fd, &info);
  if (rc < 0) {
    fprintf(stderr, "error getting block info, ioctl result code: %zd\n", rc);
    return NULL;
  }
  *blocksize_out = info.block_size;

  if (*blocksize_out < 1) {
    fprintf(stderr, "Device reports block size of %" PRIu64 ", abort!\n",
            *blocksize_out);
    return NULL;
  }

  gpt_device_t *gpt;

  // gpt_device_init produces output we want to suppress, so kidnap stdout
  // TODO(jmatt) Remove this hyjinx when lib-gpt stops writing to stdout
  int backup = dup(1);
  close(1);

  rc = gpt_device_init(fd, info.block_size, info.block_count, &gpt);

  // restore stdout
  fflush(stdout);
  dup2(backup, 1);
  close(backup);

  if (rc < 0) {
    fprintf(stderr, "error reading GPT, result code: %zd \n", rc);
    return NULL;
  } else if (!gpt->valid) {
    return NULL;
  }

  return gpt;
}

/*
 * Attempt to open the given path. If successful, returns the file descriptor
 * associated with the opened path in read only mode.
 */
int open_device_ro(const char *dev_path) {
  int fd = open(dev_path, O_RDONLY);
  if (fd < 0) {
    fprintf(stderr, "Could not read device at %s, open reported error:%s\n",
            dev_path, strerror(errno));
  }

  return fd;
}

zx_status_t find_disk_by_guid(DIR *dir, const char *dir_path,
                              uint8_t (*disk_guid)[GPT_GUID_LEN],
                              gpt_device_t **install_dev_out,
                              char *disk_path_out, ssize_t max_len) {
  strcpy(disk_path_out, dir_path);
  const size_t base_len = strlen(disk_path_out);
  const size_t buffer_remaining = max_len - base_len - 1;
  uint64_t block_size;
  gpt_device_t *install_dev = NULL;
  uint8_t guid_targ[GPT_GUID_LEN];
  *install_dev_out = NULL;

  for (ssize_t rc =
           get_next_file_path(dir, buffer_remaining, &disk_path_out[base_len]);
       rc >= 0; rc = get_next_file_path(dir, buffer_remaining,
                                        &disk_path_out[base_len])) {
    if (rc > 0) {
      fprintf(stderr, "Device path length overrun by %zd characters\n", rc);
      continue;
    }
    // open device read-only
    int fd = open_device_ro(disk_path_out);
    if (fd < 0) {
      continue;
    }

    install_dev = read_gpt(fd, &block_size);
    close(fd);

    if (install_dev != NULL) {
      gpt_device_get_header_guid(install_dev, &guid_targ);
      if (!memcmp(guid_targ, disk_guid, GPT_GUID_LEN)) {
        *install_dev_out = install_dev;
        return ZX_OK;
      }
      gpt_device_release(install_dev);
    }
  }

  strcpy(disk_path_out, "");
  return ZX_ERR_NOT_FOUND;
}
