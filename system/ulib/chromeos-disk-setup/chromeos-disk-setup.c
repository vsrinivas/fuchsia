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

static const uint8_t fvm_guid[GPT_GUID_LEN] = GUID_FVM_VALUE;
static const uint8_t kern_guid[GPT_GUID_LEN] = GUID_CROS_KERNEL_VALUE;
static const uint8_t root_guid[GPT_GUID_LEN] = GUID_CROS_ROOT_VALUE;
static const uint8_t state_guid[GPT_GUID_LEN] = GUID_CROS_STATE_VALUE;
static const uint8_t syscfg_guid[GPT_GUID_LEN] = GUID_SYS_CONFIG_VALUE;

// this value is shared with device-partitioner.cpp
static const uint64_t MIN_FVM_SIZE = 8LU * (1 << 30);

static const uint64_t SYSCFG_SIZE = 1 << 20;

// part_name_eql compares the name field in the given partition to the cstring
// name, returning true if the partition name is equal to name and zero-padded.
static bool part_name_eql(const gpt_partition_t *part, const char *name) {
    if (part == NULL) {
        return false;
    }
    char buf[GPT_NAME_LEN] = {0};
    utf16_to_cstring(&buf[0], (const uint16_t*)part->name, GPT_NAME_LEN/2);
    return !strncmp(buf, name, GPT_NAME_LEN);
}

// part_name_guid_eql returns true if the given partition has the given name and
// the given type GUID, false otherwise.
static bool part_name_guid_eql(const gpt_partition_t *part, const char *name, const uint8_t guid[GPT_GUID_LEN] ) {
    if (part == NULL) {
        return false;
    }
    if (!memcmp(part->type, &guid[0], GPT_GUID_LEN) && part_name_eql(part, name)) {
        return true;
    }
    return false;
}

// part_size_gte returns true if the partition size is greater than or equal to
// the size given, false otherwise.
static bool part_size_gte(gpt_partition_t *part, uint64_t size, uint64_t block_size) {
    if (part == NULL) {
        return false;
    }
    uint64_t size_in_blocks = part->last - part->first + 1;
    return size_in_blocks * block_size >= size;
}

// find_by_type finds the first partition matching the given type guid.
static gpt_partition_t* find_by_type(const gpt_device_t* gpt, const uint8_t type_guid[GPT_GUID_LEN]) {
    for(size_t i = 0; i < PARTITIONS_COUNT; ++i) {
        gpt_partition_t* p = gpt->partitions[i];
        if (p == NULL) {
            continue;
        }
        if (!memcmp(p->type, &type_guid[0], GPT_GUID_LEN)) {
            return p;
        }
    }
    return NULL;
}

// find_by_type_and_name finds the first partition matching the given type guid and name.
static gpt_partition_t* find_by_type_and_name(const gpt_device_t* gpt, const uint8_t type_guid[GPT_GUID_LEN], const char *name) {
    for(size_t i = 0; i < PARTITIONS_COUNT; ++i) {
        gpt_partition_t* p = gpt->partitions[i];
        if (p == NULL) {
            continue;
        }
        if (part_name_guid_eql(p, name, type_guid)) {
            return p;
        }
    }
    return NULL;
}

bool is_cros(const gpt_device_t* gpt) {
    uint8_t roots = 0;
    uint8_t kerns = 0;
    bool state = false;

    for (int i = 0; i < PARTITIONS_COUNT; ++i) {
        gpt_partition_t* p = gpt->partitions[i];
        if (p == NULL) {
            continue;
        }

        if (!memcmp(p->type, root_guid, GPT_GUID_LEN) &&
            (part_name_eql(p, "ROOT-A") || part_name_eql(p, "ROOT-B"))) {
            roots++;
        } else if (!memcmp(p->type, kern_guid, GPT_GUID_LEN) &&
                   (part_name_eql(p, "KERN-A") || part_name_eql(p, "KERN-B"))) {
            kerns++;
        } else if (!memcmp(p->type, state_guid, GPT_GUID_LEN) &&
                   part_name_eql(p, "STATE")) {
            state = true;
        }
    }

    return state && roots >= 2 && kerns >= 2;
}

// Find a contiguous run of free space on the disk at least blocks_req in length.
// If space is found, true is returned and out_hole_start and out_hole_end
// contain the first free and last free blocks in a contiguous run.
static bool find_space(gpt_device_t* gpt,
                           const uint64_t blocks_req,
                           uint64_t *out_hole_start,
                           uint64_t *out_hole_end) {

    gpt_partition_t* parts[PARTITIONS_COUNT] = {0};
    memcpy(&parts, &gpt->partitions, sizeof(gpt_partition_t*) * PARTITIONS_COUNT);

    // XXX(raggi): once the lib supports more and less than PARTITIONS_COUNT,
    // this will need to be fixed (as will many loops).
    gpt_sort_partitions(parts, PARTITIONS_COUNT);

    uint64_t first_usable, last_usable;
    gpt_device_range(gpt, &first_usable, &last_usable);

    uint64_t prev_end = first_usable - 1;
    for (size_t i = 0; i < PARTITIONS_COUNT; i++) {
        if (parts[i] == NULL) {
            continue;
        }

        // TODO(raggi): find out how the tests end up making this state
        if (parts[i]->first >= last_usable || parts[i]->last >= last_usable) {
            break;
        }

        uint64_t gap = parts[i]->first - (prev_end + 1);
        if (gap >= blocks_req) {
            *out_hole_start = prev_end + 1;
            *out_hole_end = parts[i]->first - 1;

            return true;
        }
        prev_end = parts[i]->last;
    }

    if (prev_end < last_usable && last_usable - (prev_end + 1) >= blocks_req) {
        *out_hole_start = prev_end + 1;
        *out_hole_end = last_usable;

        return true;
    }

    return false;
}

// is_ready_to_pave returns true if there exist partitions for:
// * ZIRCON-A is a GUID_CROS_KERNEL_VALUE at least sz_kern in size.
// * ZIRCON-B is a GUID_CROS_KERNEL_VALUE at least sz_kern in size.
// * ZIRCON-R is a GUID_CROS_KERNEL_VALUE at least sz_kern in size.
// * FVM      is a GUID_FVM_VALUE         at least sz_kern * 8 in size
bool is_ready_to_pave(const gpt_device_t* gpt, const block_info_t* blk_info,
                      const uint64_t sz_kern) {

    bool found_zircon_a = false, found_zircon_b = false, found_zircon_r = false;
    bool found_fvm = false, found_syscfg = false;

    for (size_t i = 0; i < PARTITIONS_COUNT; i++) {
        gpt_partition_t *part = gpt->partitions[i];
        if (part == NULL) {
            continue;
        }
        if (!memcmp(part->type, fvm_guid, GPT_GUID_LEN)) {
            if (!part_size_gte(part, MIN_FVM_SIZE, blk_info->block_size)) {
                continue;
            }
            found_fvm = true;
            continue;
        }
        if (!memcmp(part->type, kern_guid, GPT_GUID_LEN)) {
            if (!part_size_gte(part, sz_kern, blk_info->block_size)) {
                continue;
            }
            if (part_name_eql(part, "ZIRCON-A")) {
                found_zircon_a = true;
            }
            if (part_name_eql(part, "ZIRCON-B")) {
                found_zircon_b = true;
            }
            if (part_name_eql(part, "ZIRCON-R")) {
                found_zircon_r = true;
            }
        }
        if (!memcmp(part->type, syscfg_guid, GPT_GUID_LEN)) {
            if (!part_size_gte(part, SYSCFG_SIZE, blk_info->block_size)) {
                continue;
            }
            found_syscfg = true;
        }
    }
    if (!found_syscfg) {
        printf("cros-disk-setup: missing syscfg (or insufficient size)\n");
    }
    if (!found_fvm) {
        printf("cros-disk-setup: missing FVM (or insufficient size)\n");
    }
    if (!found_zircon_a || !found_zircon_b || !found_zircon_r) {
        printf("cros-disk-setup: missing one or more kernel partitions\n");
    }

    return found_zircon_a && found_zircon_b && found_zircon_r && found_fvm && found_syscfg;
}

// create a GPT entry with the supplied attributes and assign it a random
// GUID. May return an error if the GUID can not be generated for the partition
// or operations on the gpt_device_t fail.
static zx_status_t create_gpt_entry(gpt_device_t* gpt, const uint64_t first,
                                    const uint64_t blks,
                                    const uint8_t type[GPT_GUID_LEN], const char* name) {
    uint8_t guid[GPT_GUID_LEN];
    zx_cprng_draw(guid, GPT_GUID_LEN);

    uint8_t tguid[GPT_GUID_LEN];
    memcpy(&tguid, type, GPT_GUID_LEN);
    if (gpt_partition_add(gpt, name, tguid, guid, first, blks, 0)) {
        return ZX_ERR_INTERNAL;
    }

    return ZX_OK;
}

zx_status_t config_cros_for_fuchsia(gpt_device_t* gpt,
                                    const block_info_t* blk_info,
                                    const uint64_t sz_kern) {

    // TODO(raggi): this ends up getting called twice, as the canonical user,
    // the paver, calls is_ready_to_pave itself in order to determine first
    // whether it will need to sync the gpt.
    if (is_ready_to_pave(gpt, blk_info, sz_kern)) {
        return ZX_OK;
    }

    // TODO(ZX-1396): The gpt_device_t may not be valid for modification if it
    // is a newly initialized GPT which has never had gpt_device_finalize or
    // gpt_device_sync called.
    if (gpt_device_finalize(gpt) != 0) {
      return ZX_ERR_INTERNAL;
    }

    // Remove the pre-existing Fuchsia partitions as when we were not already
    // pave-able and we're paving, assume that we want to tend toward a golden
    // layout. This also avoids any additional complexity that could arise from
    // intermediate gaps between these partitions.

    gpt_partition_t *p;
    if ((p = find_by_type_and_name(gpt, kern_guid, "ZIRCON-A")) != NULL) {
        gpt_partition_remove(gpt, p->guid);
    }
    if ((p = find_by_type_and_name(gpt, kern_guid, "ZIRCON-B")) != NULL) {
        gpt_partition_remove(gpt, p->guid);
    }
    if ((p = find_by_type_and_name(gpt, kern_guid, "ZIRCON-R")) != NULL) {
        gpt_partition_remove(gpt, p->guid);
    }
    if ((p = find_by_type(gpt, fvm_guid)) != NULL) {
        gpt_partition_remove(gpt, p->guid);
    }
    if ((p = find_by_type_and_name(gpt, syscfg_guid, "SYSCFG")) != NULL) {
        gpt_partition_remove(gpt, p->guid);
    }

    // Space is required for 3 kernel partitions and one FVM partition that is
    // at least 8 kernels in size.
    uint64_t needed_space = sz_kern * 3 + MIN_FVM_SIZE + SYSCFG_SIZE;

    // see if a contiguous block of space is available for space needed
    uint64_t blocks_needed = howmany(needed_space, blk_info->block_size);

    uint64_t hole_start, hole_end;
    bool found_hole = find_space(gpt, blocks_needed, &hole_start, &hole_end);

    // TODO(raggi): find a good heuristic to detect "old-paver" behavior, and if
    // we can detect that, remove the -C's, otherwise leave them alone.

    // First try removing the kernc and rootc partitions, as they're often a good fit for us:
    if (!found_hole) {
        // Some partitions were not large enough. If we found a KERN-C or a ROOT-C, delete them:
        if ((p = find_by_type_and_name(gpt, kern_guid, "KERN-C")) != NULL) {
            gpt_partition_remove(gpt, p->guid);
        }
        if ((p = find_by_type_and_name(gpt, root_guid, "ROOT-C")) != NULL) {
            gpt_partition_remove(gpt, p->guid);
        }

        found_hole = find_space(gpt, blocks_needed, &hole_start, &hole_end);
    }

    // Still not enough contiguous space is available on disk, try shrinking STATE
    if (!found_hole && (p = find_by_type_and_name(gpt, state_guid, "STATE")) != NULL)  {
        uint64_t min_state_sz_blks = howmany(MIN_SZ_STATE, blk_info->block_size);

        // TODO (TO-607) consider if there is free space on either side of STATE

        // The STATE partition is expected to be at the end of the GPT in cros,
        // and can be shrunk in order to make space for use cases such as this.
        // Here we first try to make the partition half of it's current size
        // (pinned to a minimum of min_sz_blks). This gives us a roughly equal
        // share of the free space on the disk.

        uint64_t state_part_blks = (p->last - p->first) + 1;
        uint64_t new_state_part_blks = state_part_blks / 2;
        if (new_state_part_blks < min_state_sz_blks) {
            new_state_part_blks = min_state_sz_blks;
        }
        uint64_t new_free_blks = state_part_blks - new_state_part_blks;

        if (new_free_blks >= blocks_needed) {
            p->first = p->first + new_free_blks;

            // find_space is re-run here as there is often a chunk of free space
            // left before the STATE partition that is not big enough for us to
            // fit, but is sensible to use which would not be used simply by
            // using the state->first offset.
            found_hole = find_space(gpt, blocks_needed, &hole_start, &hole_end);
        }
    }

    if (!found_hole) {
        return ZX_ERR_NO_SPACE;
    }

    printf("cros-disk-setup: Creating SYSCFG\n");
    zx_status_t status;
    uint64_t sz_syscfg_blks = howmany(SYSCFG_SIZE, blk_info->block_size);
    if ((status = create_gpt_entry(gpt, hole_start, sz_syscfg_blks, syscfg_guid, "SYSCFG")) != ZX_OK) {
        printf("cros-disk-setup: Error creating SYSCFG: %d\n", status);
        return ZX_ERR_INTERNAL;
    }
    hole_start += sz_syscfg_blks;

    uint64_t sz_kern_blks = howmany(sz_kern, blk_info->block_size);

    // Create GPT entries for ZIRCON-A, ZIRCON-B, ZIRCON-R and FVM if needed.
    const char *kernel_names[3] = { "ZIRCON-A", "ZIRCON-B", "ZIRCON-R" };
    for(size_t i = 0; i < 3; ++i) {
        printf("cros-disk-setup: Creating %s\n", kernel_names[i]);
        if ((status = create_gpt_entry(gpt, hole_start, sz_kern_blks, kern_guid, kernel_names[i])) != ZX_OK) {
            printf("cros-disk-setup: Error creating %s: %d\n", kernel_names[i], status);
            return ZX_ERR_INTERNAL;
        }
        hole_start += sz_kern_blks;
    }

    printf("cros-disk-setup: Creating FVM\n");

    // TODO(raggi): add this after the test setup supports it.
    // // clear the FVM superblock to ensure that a new FVM will be created there.
    // if (gpt_partition_clear(gpt, hole_start, 1) != 0) {
    //     printf("Error clearing FVM superblock.\n");
    //     return ZX_ERR_INTERNAL;
    // }

    // The created FVM partition will fill the available free space.
    if ((status = create_gpt_entry(gpt, hole_start, (hole_end - hole_start),
                        fvm_guid, "fvm")) != ZX_OK) {
        printf("cros-disk-setup: Error creating FVM\n");
        return status;
    }

    // TODO(raggi): add this once the test setup supports it.
    // if (!gpt_device_finalize(gpt)) {
    //     printf("Error finalizing GPT\n");
    //     return ZX_ERR_INTERNAL;
    // }
    return ZX_OK;
}
