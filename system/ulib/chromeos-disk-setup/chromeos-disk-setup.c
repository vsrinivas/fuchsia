// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>

#include <chromeos-disk-setup/chromeos-disk-setup.h>
#include <gpt/cros.h>
#include <gpt/gpt.h>
#include <zircon/device/block.h>
#include <zircon/errors.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

bool is_cros(const gpt_device_t* gpt) {
    uint8_t kern_guid[GPT_GUID_LEN] = GUID_CROS_KERNEL_VALUE;
    uint8_t root_guid[GPT_GUID_LEN] = GUID_CROS_ROOT_VALUE;
    uint8_t state_guid[GPT_GUID_LEN] = GUID_CROS_STATE_VALUE;
    gpt_partition_t* const* parts = gpt->partitions;

    uint8_t roots = 0;
    uint8_t kerns = 0;
    bool state = false;
    char buf[GPT_NAME_LEN / 2 + 1];

    for (int i = 0; parts[i] != NULL; ++i) {
        const gpt_partition_t* p = parts[i];

        memset(buf, 0, GPT_NAME_LEN / 2 + 1);
        utf16_to_cstring(buf, (uint16_t*)p->name, GPT_NAME_LEN / 2);
        if (!memcmp(p->type, root_guid, GPT_GUID_LEN) &&
            (!memcmp("ROOT-A", buf, 7) || !memcmp("ROOT-B", buf, 7))) {
            roots++;
        } else if (!memcmp(p->type, kern_guid, GPT_GUID_LEN) &&
                   (!memcmp("KERN-A", buf, 7) || !memcmp("KERN-B", buf, 7))) {
            kerns++;
        } else if (!memcmp(p->type, state_guid, GPT_GUID_LEN) &&
                   !memcmp("STATE", buf, 6)) {
            state = true;
        }
    }

    return state && roots == 2 && kerns == 2;
}

// Attempt to expand the target partition without moving it.
//  parts     - the table of all disk partitions, sorted by start block index
//  part_idx  - partition that we want to expand
//  sz        - the size in bytes the partition wants to be
//  num_parts - total number of partitions of the disk
//  disk_info - information about the disk hosting the partitions
//
// Returns a boolean indicating whether the resize was successful or not. If it
// was successful, the partition record is updated, otherwise it is untouched.
static bool expand_in_place(gpt_partition_t** parts, const int16_t part_idx,
                            const uint64_t sz, const uint16_t num_parts,
                            const block_info_t* disk_info) {
    if (part_idx < 0) {
        return false;
    }

    gpt_partition_t* targ = parts[part_idx];
    uint64_t new_first = targ->first;
    uint64_t new_last = targ->last;
    uint64_t blocks_needed = howmany(sz, disk_info->block_size);
    blocks_needed -= (targ->last - targ->first + 1);
    uint64_t prev_part_end;
    if (part_idx > 0) {
        prev_part_end = parts[part_idx - 1]->last;
    } else {
        prev_part_end = gpt_device_get_size_blocks(disk_info->block_size) - 1;
    }

    uint64_t gap = targ->first - prev_part_end - 1;
    if (gap >= blocks_needed) {
        new_first -= blocks_needed;
        blocks_needed = 0;
    } else {
        new_first -= gap;
        blocks_needed -= gap;
    }

    uint64_t next_part_start;
    // see if we can grow the partition at the end
    if (num_parts > part_idx + 1) {
        next_part_start = parts[part_idx + 1]->first;
    } else {
        next_part_start = disk_info->block_count -
                          gpt_device_get_size_blocks(disk_info->block_size);
    }

    gap = next_part_start - parts[part_idx]->last - 1;
    if (gap >= blocks_needed) {
        new_last = targ->last + blocks_needed;
        blocks_needed = 0;
    } else {
        new_last = targ->last + gap;
        blocks_needed -= gap;
    }

    // see if the new location of beginning and end of partition
    // are enough, if so, modify our in-memory GPT, otherwise we
    // can't expand in-place
    if ((new_last - new_first + 1) * disk_info->block_size >= sz) {
        targ->first = new_first;
        targ->last = new_last;
        return true;
    } else {
        return false;
    }
}

// Find parition indexes based on GUID. If the partition is not found, the
// corresponding out-variable will be set to -1.
static void find_partitions(gpt_partition_t* const* parts, int16_t* kern_out,
                            int16_t* root_out, int16_t* fvm_out,
                            int16_t* state_out, const uint16_t part_count) {
    uint8_t fvm_guid[GPT_GUID_LEN] = GUID_FVM_VALUE;
    uint8_t kern_guid[GPT_GUID_LEN] = GUID_CROS_KERNEL_VALUE;
    uint8_t root_guid[GPT_GUID_LEN] = GUID_CROS_ROOT_VALUE;
    uint8_t state_guid[GPT_GUID_LEN] = GUID_CROS_STATE_VALUE;

    *root_out = -1;
    *kern_out = -1;
    *fvm_out = -1;
    *state_out = -1;
    char buf[GPT_NAME_LEN / 2 + 1];

    for (int i = 0; i < part_count && parts[i] != NULL &&
                    (*root_out < 0 || *kern_out < 0 || *fvm_out < 0 || *state_out < 0);
         ++i) {
        const gpt_partition_t* part = parts[i];

        memset(buf, 0, GPT_NAME_LEN / 2 + 1);
        utf16_to_cstring(buf, (uint16_t*)part->name, GPT_NAME_LEN / 2);
        if (!memcmp(part->type, root_guid, GPT_GUID_LEN) &&
            !memcmp("ROOT-C", buf, 7)) {
            *root_out = i;
        } else if (!memcmp(part->type, kern_guid, GPT_GUID_LEN) &&
                   !memcmp("KERN-C", buf, 7)) {
            *kern_out = i;
        } else if (!memcmp(part->type, fvm_guid, GPT_GUID_LEN)) {
            *fvm_out = i;
        } else if (!memcmp(part->type, state_guid, GPT_GUID_LEN) &&
                   !memcmp("STATE", buf, 6)) {
            *state_out = i;
        }
    }
}

// Find requested amount of space. Find the lowest block offset that
// has that amount of available space. If space is not found, 0 is returned.
// 0 is not a valid value since the GPT itself lives in the early part of
static uint64_t find_space(gpt_partition_t* const* parts,
                           const uint16_t part_count, const uint64_t blocks_req,
                           const block_info_t* d_info) {
    uint64_t prev_end = gpt_device_get_size_blocks(d_info->block_size) - 1;
    for (uint16_t i = 0; i < part_count; i++) {
        uint64_t gap = parts[i]->first - prev_end - 1;
        if (gap >= blocks_req) {
            return prev_end + 1;
        }
        prev_end = parts[i]->last;
    }

    uint64_t last_usable =
        d_info->block_count - gpt_device_get_size_blocks(d_info->block_size) - 1;
    if (prev_end <= last_usable && last_usable - prev_end >= blocks_req) {
        return prev_end + 1;
    }

    return 0;
}

bool is_ready_to_pave(const gpt_device_t* gptd, const block_info_t* blk_info,
                      const uint64_t sz_kern, const uint64_t sz_root,
                      const bool fvm_req) {

    bool root = false;
    bool kern = false;
    bool fvm = !fvm_req;
    uint64_t max_blks_state = howmany(MIN_SZ_STATE, blk_info->block_size);

    int16_t k_idx = -1;
    int16_t r_idx = -1;
    int16_t f_idx = -1;
    int16_t s_idx = -1;
    uint16_t count = 0;
    for (; gptd->partitions[count] != NULL; count++)
        ;

    find_partitions(gptd->partitions, &k_idx, &r_idx, &f_idx, &s_idx, count);

    gpt_partition_t* kern_part = NULL;
    if (k_idx > -1) {
        kern_part = gptd->partitions[k_idx];
    }
    gpt_partition_t* root_part = NULL;
    if (r_idx > -1) {
        root_part = gptd->partitions[r_idx];
    }
    gpt_partition_t* fvm_part = NULL;
    if (f_idx > -1) {
        fvm_part = gptd->partitions[f_idx];
    }

    if (kern_part != NULL &&
        (kern_part->last - kern_part->first + 1) * blk_info->block_size >= sz_kern) {
        kern = true;
    }

    if (root_part != NULL &&
        (root_part->last - root_part->first + 1) * blk_info->block_size >= sz_root) {
        root = true;
    }

    if (fvm_part != NULL) {
        fvm = true;
    }

    if (fvm && root && kern) {
        return true;
    }

    if (!root || !kern) {
        return false;
    }

    char buf[GPT_NAME_LEN / 2 + 1];
    uint8_t state_guid[GPT_GUID_LEN] = GUID_CROS_STATE_VALUE;
    // the fvm partition does not exist, make sure the state partition is
    // as no larger than the allowed size to allow as much space as possible
    // for fvm.
    for (uint16_t i = 0; !fvm && gptd->partitions[i] != NULL; i++) {
        gpt_partition_t* part = gptd->partitions[i];
        memset(buf, 0, GPT_NAME_LEN / 2 + 1);
        utf16_to_cstring(buf, (uint16_t*)part->name, GPT_NAME_LEN / 2);
        if (!memcmp(part->type, state_guid, GPT_GUID_LEN) &&
            !memcmp("STATE", buf, 6) &&
            part->last - part->first + 1 <= max_blks_state) {
            fvm = true;
        }
    }

    return root && kern && fvm;
}

// create a GPT entry with the supplied attributes and assign it a random
// GUID. May return an error if the GUID can not be generated for the partition
// or operations on the gpt_device_t fail.
static zx_status_t create_gpt_entry(gpt_device_t* dev, const uint64_t first,
                                    const uint64_t sz, const uint32_t blk_sz,
                                    uint8_t* type, const char* name) {
    uint64_t blks = howmany(sz, blk_sz);
    uint8_t guid[GPT_GUID_LEN];

    size_t gened = 0;
    zx_status_t rc = zx_cprng_draw(guid, GPT_GUID_LEN, &gened);
    if (rc != ZX_OK) {
        return rc;
    } else if(gened != GPT_GUID_LEN) {
        return ZX_ERR_INTERNAL;
    }

    // The gpt_device_t may not be valid for use with gpt_partition_add if
    // it is a newly initialized GPT which has never had gpt_device_finalize
    // or gpt_device_sync on, call gpt_device_finalize to be safe.
    // Remove when ZX-1396 is fixed.
    if (gpt_device_finalize(dev)) {
      return ZX_ERR_INTERNAL;
    }

    if (gpt_partition_add(dev, name, type, guid, first, blks, 0)) {
        return ZX_ERR_INTERNAL;
    }

    return ZX_OK;
}

zx_status_t config_cros_for_fuchsia(gpt_device_t* gpt,
                                    const block_info_t* blk_info,
                                    const uint64_t sz_kern,
                                    const uint64_t sz_root,
                                    const bool fvm_req) {
    uint8_t kern_guid[GPT_GUID_LEN] = GUID_CROS_KERNEL_VALUE;
    uint8_t root_guid[GPT_GUID_LEN] = GUID_CROS_ROOT_VALUE;

    if (is_ready_to_pave(gpt, blk_info, sz_kern, sz_root, fvm_req)) {
        return ZX_OK;
    }

    uint64_t sz_state = MIN_SZ_STATE;

    // mark everything as unknown
    bool root = false;
    bool kern = false;
    bool fvm = !fvm_req;

    uint16_t num_parts = 0;
    for (; gpt->partitions[num_parts] != NULL; ++num_parts)
        ;
    gpt_partition_t** parts = malloc(num_parts * sizeof(gpt_partition_t*));
    if (parts == NULL) {
        return ZX_ERR_NO_MEMORY;
    }

    gpt_sort_partitions(gpt->partitions, parts, num_parts);

    int16_t kern_idx = -1;
    int16_t root_idx = -1;
    int16_t fvm_idx = -1;
    int16_t state_idx = -1;
    find_partitions(parts, &kern_idx, &root_idx, &fvm_idx, &state_idx, num_parts);

    // see which partitions already have an entry
    gpt_partition_t* kern_part = NULL;
    if (kern_idx > -1) {
        kern_part = parts[kern_idx];
    }
    gpt_partition_t* root_part = NULL;
    if (root_idx > -1) {
        root_part = parts[root_idx];
    }
    gpt_partition_t* fvm_part = NULL;
    if (fvm_idx > -1) {
        fvm_part = parts[fvm_idx];
    }

    // check which existing partitions are large enough as they are
    if (kern_part != NULL &&
        (kern_part->last - kern_part->first + 1) * blk_info->block_size >= sz_kern) {
        kern = true;
    }

    if (root_part != NULL &&
        (root_part->last - root_part->first + 1) * blk_info->block_size >= sz_root) {
        root = true;
    }

    if (fvm_part != NULL) {
        fvm = true;
    }

    if (fvm && root && kern) {
        free(parts);
        return ZX_OK;
    }

    // some partitions we're unavailable or not large enough
    uint64_t needed_space = 0;

    // see which partitions can be expanded in place and which need to be moved
    if (!kern) {
        if (kern_part != NULL && expand_in_place(parts, kern_idx, sz_kern,
                                                 num_parts, blk_info)) {
            kern = true;
        } else {
            needed_space += sz_kern;
        }
    }

    if (!root) {
        if (root_part != NULL && expand_in_place(parts, root_idx, sz_root,
                                                 num_parts, blk_info)) {
            root = true;
        } else {
            needed_space += sz_root;
        }
    }

    // some partitions don't exist or couldn't be expanded in place
    if (needed_space > 0) {
        // see if a contiguous block of space is available for space needed
        uint64_t blocks_needed = howmany(needed_space, blk_info->block_size);
        uint64_t hole = find_space(parts, num_parts, blocks_needed, blk_info);

        // not enough contiguous space is available on disk, try shrinking STATE
        if (hole == 0 && state_idx > -1) {
            gpt_partition_t* state_part = parts[state_idx];
            uint64_t min_sz_blks = howmany(sz_state, blk_info->block_size);
            // TODO (TO-607) consider if there is free space on either side of STATE
            if (state_part->last - state_part->first + 1 >=
                min_sz_blks + blocks_needed) {
                hole = state_part->first;
                state_part->first += blocks_needed;
            }
        }

        // we found or made enough available space
        if (hole > 0) {
            // see if we need to create GPT entries for either root-c or kern-c
            if (kern_part == NULL) {
                const char* k_name = "KERN-C";
                if (create_gpt_entry(gpt, hole, sz_kern, blk_info->block_size,
                                    kern_guid, k_name) != ZX_OK) {
                    free(parts);
                    return ZX_ERR_INTERNAL;
                }
                kern = true;
                needed_space -= sz_kern;
                hole += howmany(sz_kern, blk_info->block_size);
            }

            if (root_part == NULL) {
                const char* r_name = "ROOT-C";
                if (create_gpt_entry(gpt, hole, sz_root, blk_info->block_size,
                                     root_guid, r_name) != ZX_OK) {
                    free(parts);
                    return ZX_ERR_INTERNAL;
                }
                root = true;
                needed_space -= sz_root;
                hole += howmany(sz_root, blk_info->block_size);
            }

            if (!kern) {
                kern_part->first = hole;
                kern_part->last = hole + howmany(sz_kern, blk_info->block_size)
                    - 1;
                hole = kern_part->last + 1;
                needed_space -= sz_kern;
                kern = true;
            }

            if (!root) {
                root_part->first = hole;
                root_part->last = hole + howmany(sz_root, blk_info->block_size)
                    - 1;
                hole = root_part->last + 1;
                needed_space -= sz_root;
                root = true;
            }
        }
    }

    if (!fvm && state_idx > -1) {
        gpt_partition_t* part = parts[state_idx];
        uint64_t state_blks = howmany(sz_state, blk_info->block_size);
        if (part->last - part->first + 1 > state_blks) {
            part->first = part->last - state_blks + 1;
        }
        fvm = true;
    }

    free(parts);

    if (fvm && root && kern) {
        return ZX_OK;
    } else {
        return ZX_ERR_BAD_STATE;
    }
}
