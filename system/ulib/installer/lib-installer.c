// Copyright 2017 The Fuchsia Authors. All rights reserved.
// User of this source code is governed by a BSD-style license that be be found
// in the LICENSE file.

#include <inttypes.h>
#include <magenta/assert.h>
#include <magenta/syscalls.h>
#include <stdio.h>
#include <string.h>

#include <installer/lib-installer.h>

/*
 * Given a pointer to an array of gpt_partition_t structs, look for a partition
 * with the matching type GUID. The table_size argument tells this function how
 * many entries there are in the array.
 * Returns
 *   * ERR_INVALID_ARGS if any pointers are null
 *   * ERR_BAD_STATE if the GPT data reports itself as invalid
 *   * ERR_NOT_FOUND if the requested partition is not present
 *   * NO_ERROR if the partition is found, in which case the index is assigned
 *     to part_id_out
 */
mx_status_t find_partition_entries(gpt_partition_t** gpt_table,
                                   const uint8_t* guid,
                                   uint16_t table_size,
                                   uint16_t* part_id_out) {
    DEBUG_ASSERT(gpt_table != NULL);
    DEBUG_ASSERT(guid != NULL);
    DEBUG_ASSERT(part_id_out != NULL);

    for (uint16_t idx = 0; idx < table_size && gpt_table[idx] != NULL; idx++) {
        uint8_t* type_ptr = gpt_table[idx]->type;
        if (!memcmp(type_ptr, guid, 16)) {
            *part_id_out = idx;
            return NO_ERROR;
        }
    }

    return ERR_NOT_FOUND;
}

/*
 * For the given partition, see if it is at least as large as min_size.
 */
bool check_partition_size(const gpt_partition_t* partition,
                          const uint64_t min_size,
                          const uint64_t block_size,
                          const char* partition_name) {
    DEBUG_ASSERT(partition != NULL);
    DEBUG_ASSERT(partition->last >= partition->first);
    DEBUG_ASSERT(partition->name != NULL);

    uint64_t block_count = partition->last - partition->first + 1;
    uint64_t partition_size =
        block_size * (partition->last - partition->first + 1);
    printf("%s has %" PRIu64 " blocks and block size of %" PRIu64 "\n",
           partition_name, block_count, block_size);

    if (partition_size < min_size) {
        fprintf(stderr, "%s partition too small, found %" PRIu64 ", but require \
                %" PRIu64  "\n",
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
 * If NO_ERROR is returned, part_index_out will contain the index in the GPT for
 * the requested partition. Additionally, the pointer at *part_info_out will
 * point at the gpt_partition_t with information for the requested partition.
 */
mx_status_t find_partition(gpt_partition_t** gpt_table,
                           const uint8_t* part_guid, uint64_t min_size,
                           uint64_t block_size, const char* part_name,
                           uint16_t table_size, uint16_t* part_index_out,
                           gpt_partition_t** part_info_out) {
    // if we find a partition, but it is the wrong size, we want to keep
    // looking down the table for something that might match, this offset
    // tracks our overall progress in the table
    uint16_t part_offset = 0;

    mx_status_t rc = NO_ERROR;
    while (rc == NO_ERROR) {
        // reduce table_size by the number of entries we've examined
        rc = find_partition_entries(gpt_table, part_guid,
                                    table_size - part_offset, part_index_out);

        gpt_partition_t* partition;
        switch (rc) {
            case ERR_NOT_FOUND:
                fprintf(stderr, "No %s partition found.\n", part_name);
                break;
            case ERR_INVALID_ARGS:
                fprintf(stderr, "Arguments are invalid for %s partition.\n",
                        part_name);
                break;
            case ERR_BAD_STATE:
                printf("GPT descriptor is invalid.\n");
                break;
            case NO_ERROR:
                partition = gpt_table[*part_index_out];
                DEBUG_ASSERT(partition->last >= partition->first);

                if (check_partition_size(partition, min_size, block_size,
                                         part_name)) {
                    *part_info_out = partition;
                    // adjust the output index by part_offset
                    *part_index_out = part_offset + *part_index_out;
                    return NO_ERROR;
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
                fprintf(stderr, "Unrecognized error finding efi parition: %d\n",
                        rc);
                break;
        }
    }

    // we didn't find a suitable partition
    return ERR_NOT_FOUND;
}
