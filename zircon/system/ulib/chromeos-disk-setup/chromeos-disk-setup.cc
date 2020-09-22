// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <zircon/assert.h>
#include <zircon/device/block.h>
#include <zircon/errors.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <chromeos-disk-setup/chromeos-disk-setup.h>
#include <gpt/cros.h>
#include <gpt/gpt.h>

namespace chromeos_disk_setup {
using gpt::GptDevice;
namespace {

constexpr uint8_t kFvmGuid[GPT_GUID_LEN] = GUID_FVM_VALUE;
constexpr uint8_t kKernGuid[GPT_GUID_LEN] = GUID_CROS_KERNEL_VALUE;
constexpr uint8_t kRootGuid[GPT_GUID_LEN] = GUID_CROS_ROOT_VALUE;
constexpr uint8_t kStateCrosGuid[GPT_GUID_LEN] = GUID_CROS_STATE_VALUE;
constexpr uint8_t kStateLinuxGuid[GPT_GUID_LEN] = GUID_LINUX_FILESYSTEM_DATA_VALUE;
constexpr uint8_t kSysCfgGuid[GPT_GUID_LEN] = GUID_SYS_CONFIG_VALUE;

// this value is shared with device-partitioner.cpp
constexpr uint64_t kMinFvmSize = 8LU * (1 << 30);

constexpr uint64_t kSysCfgSize = 1 << 20;

// part_name_eql compares the name field in the given partition to the cstring
// name, returning true if the partition name is equal to name and zero-padded.
bool part_name_eql(const gpt_partition_t* part, const char* name) {
  if (part == NULL) {
    return false;
  }
  char buf[GPT_NAME_LEN] = {0};
  utf16_to_cstring(&buf[0], (const uint16_t*)part->name, GPT_NAME_LEN / 2);
  // We use a case-insenstive comparison to be compatible with the previous naming scheme.
  // On a ChromeOS device, all of the kernel partitions share a common GUID type, so we
  // distinguish Zircon kernel partitions based on name.
  return !strncasecmp(buf, name, GPT_NAME_LEN);
}

// part_name_guid_eql returns true if the given partition has the given name and
// the given type GUID, false otherwise.
bool part_name_guid_eql(const gpt_partition_t* part, const char* name,
                        const uint8_t guid[GPT_GUID_LEN]) {
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
bool part_size_gte(gpt_partition_t* part, uint64_t size, uint64_t block_size) {
  if (part == NULL) {
    return false;
  }
  uint64_t size_in_blocks = part->last - part->first + 1;
  return size_in_blocks * block_size >= size;
}

// find_by_type finds the first partition matching the given type guid.
gpt_partition_t* find_by_type(const GptDevice* gpt, const uint8_t type_guid[GPT_GUID_LEN]) {
  for (uint32_t i = 0; i < gpt::kPartitionCount; ++i) {
    gpt_partition_t* p = gpt->GetPartition(i);
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
gpt_partition_t* find_by_type_and_name(const GptDevice* gpt, const uint8_t type_guid[GPT_GUID_LEN],
                                       const char* name) {
  for (uint32_t i = 0; i < gpt::kPartitionCount; ++i) {
    gpt_partition_t* p = gpt->GetPartition(i);
    if (p == NULL) {
      continue;
    }
    if (part_name_guid_eql(p, name, type_guid)) {
      return p;
    }
  }
  return NULL;
}

// Find a contiguous run of free space on the disk at least blocks_req in length.
// If space is found, true is returned and out_hole_start and out_hole_end
// contain the first free and last free blocks in a contiguous run.
bool find_space(GptDevice* gpt, const uint64_t blocks_req, uint64_t* out_hole_start,
                uint64_t* out_hole_end) {
  gpt_partition_t* parts[gpt::kPartitionCount] = {0};
  for (uint32_t i = 0; i < gpt::kPartitionCount; i++) {
    parts[i] = gpt->GetPartition(i);
  }

  // XXX(raggi): once the lib supports more and less than gpt::kPartitionCount,
  // this will need to be fixed (as will many loops).
  gpt_sort_partitions(parts, gpt::kPartitionCount);

  uint64_t first_usable, last_usable;
  ZX_ASSERT(gpt->Range(&first_usable, &last_usable) == ZX_OK);

  uint64_t prev_end = first_usable - 1;
  for (uint32_t i = 0; i < gpt::kPartitionCount; i++) {
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

// create a GPT entry with the supplied attributes and assign it a random
// GUID. May return an error if the GUID can not be generated for the partition
// or operations on the gpt_device_t fail.
zx_status_t create_gpt_entry(GptDevice* gpt, const uint64_t first, const uint64_t blks,
                             const uint8_t type[GPT_GUID_LEN], const char* name) {
  uint8_t guid[GPT_GUID_LEN];
  zx_cprng_draw(guid, GPT_GUID_LEN);

  uint8_t tguid[GPT_GUID_LEN];
  memcpy(&tguid, type, GPT_GUID_LEN);
  if (gpt->AddPartition(name, tguid, guid, first, blks, 0) != ZX_OK) {
    return ZX_ERR_INTERNAL;
  }

  return ZX_OK;
}

}  // namespace

__BEGIN_CDECLS

bool is_cros(const GptDevice* gpt) {
  uint8_t roots = 0;
  uint8_t kerns = 0;
  bool state = false;

  for (uint32_t i = 0; i < gpt::kPartitionCount; ++i) {
    gpt_partition_t* p = gpt->GetPartition(i);
    if (p == NULL) {
      continue;
    }

    if (!memcmp(p->type, kRootGuid, GPT_GUID_LEN) &&
        (part_name_eql(p, "ROOT-A") || part_name_eql(p, "ROOT-B"))) {
      roots++;
    } else if (!memcmp(p->type, kKernGuid, GPT_GUID_LEN) &&
               (part_name_eql(p, "KERN-A") || part_name_eql(p, "KERN-B"))) {
      kerns++;
    } else if ((!memcmp(p->type, kStateCrosGuid, GPT_GUID_LEN) ||
                !memcmp(p->type, kStateLinuxGuid, GPT_GUID_LEN)) &&
               part_name_eql(p, "STATE")) {
      // Note that STATE GUID type can either be cros_data or, in the case of a
      // freshly recovered device, linux_filesystem
      state = true;
    }
  }

  return state && roots >= 2 && kerns >= 2;
}

// is_ready_to_pave returns true if there exist partitions for:
// * ZIRCON-A is a GUID_CROS_KERNEL_VALUE at least sz_kern in size.
// * ZIRCON-B is a GUID_CROS_KERNEL_VALUE at least sz_kern in size.
// * ZIRCON-R is a GUID_CROS_KERNEL_VALUE at least sz_kern in size.
// * FVM      is a GUID_FVM_VALUE         at least sz_kern * 8 in size
bool is_ready_to_pave(const GptDevice* gpt, const fuchsia_hardware_block_BlockInfo* blk_info,
                      const uint64_t sz_kern) {
  bool found_zircon_a = false, found_zircon_b = false, found_zircon_r = false;
  bool found_fvm = false, found_syscfg = false;

  for (uint32_t i = 0; i < gpt::kPartitionCount; i++) {
    gpt_partition_t* part = gpt->GetPartition(i);
    if (part == NULL) {
      continue;
    }
    if (!memcmp(part->type, kFvmGuid, GPT_GUID_LEN)) {
      if (!part_size_gte(part, kMinFvmSize, blk_info->block_size)) {
        continue;
      }
      found_fvm = true;
      continue;
    }
    if (!memcmp(part->type, kKernGuid, GPT_GUID_LEN)) {
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
    if (!memcmp(part->type, kSysCfgGuid, GPT_GUID_LEN)) {
      if (!part_size_gte(part, kSysCfgSize, blk_info->block_size)) {
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

zx_status_t config_cros_for_fuchsia(GptDevice* gpt,
                                    const fuchsia_hardware_block_BlockInfo* blk_info,
                                    const uint64_t sz_kern) {
  // TODO(raggi): this ends up getting called twice, as the canonical user,
  // the paver, calls is_ready_to_pave itself in order to determine first
  // whether it will need to sync the gpt.
  if (is_ready_to_pave(gpt, blk_info, sz_kern)) {
    return ZX_OK;
  }

  // TODO(fxbug.dev/31298): The gpt_device_t may not be valid for modification if it
  // is a newly initialized GPT which has never had gpt_device_finalize or
  // gpt_device_sync called.
  if (gpt->Finalize() != ZX_OK) {
    return ZX_ERR_INTERNAL;
  }

  // Remove the pre-existing Fuchsia partitions as when we were not already
  // pave-able and we're paving, assume that we want to tend toward a golden
  // layout. This also avoids any additional complexity that could arise from
  // intermediate gaps between these partitions.

  gpt_partition_t* p;
  if ((p = find_by_type_and_name(gpt, kKernGuid, "ZIRCON-A")) != NULL) {
    ZX_ASSERT(gpt->RemovePartition(p->guid) == ZX_OK);
  }
  if ((p = find_by_type_and_name(gpt, kKernGuid, "ZIRCON-B")) != NULL) {
    ZX_ASSERT(gpt->RemovePartition(p->guid) == ZX_OK);
  }
  if ((p = find_by_type_and_name(gpt, kKernGuid, "ZIRCON-R")) != NULL) {
    ZX_ASSERT(gpt->RemovePartition(p->guid) == ZX_OK);
  }
  if ((p = find_by_type(gpt, kFvmGuid)) != NULL) {
    ZX_ASSERT(gpt->RemovePartition(p->guid) == ZX_OK);
  }
  if ((p = find_by_type_and_name(gpt, kSysCfgGuid, "SYSCFG")) != NULL) {
    ZX_ASSERT(gpt->RemovePartition(p->guid) == ZX_OK);
  }

  // Space is required for 3 kernel partitions and one FVM partition that is
  // at least 8 kernels in size.
  uint64_t needed_space = sz_kern * 3 + kMinFvmSize + kSysCfgSize;

  // see if a contiguous block of space is available for space needed
  uint64_t blocks_needed = howmany(needed_space, blk_info->block_size);

  uint64_t hole_start, hole_end;
  bool found_hole = find_space(gpt, blocks_needed, &hole_start, &hole_end);

  // TODO(raggi): find a good heuristic to detect "old-paver" behavior, and if
  // we can detect that, remove the -C's, otherwise leave them alone.

  // First try removing the kernc and rootc partitions, as they're often a good fit for us:
  if (!found_hole) {
    // Some partitions were not large enough. If we found a KERN-C or a ROOT-C, delete them:
    if ((p = find_by_type_and_name(gpt, kKernGuid, "KERN-C")) != NULL) {
      ZX_ASSERT(gpt->RemovePartition(p->guid) == ZX_OK);
    }
    if ((p = find_by_type_and_name(gpt, kRootGuid, "ROOT-C")) != NULL) {
      ZX_ASSERT(gpt->RemovePartition(p->guid) == ZX_OK);
    }

    found_hole = find_space(gpt, blocks_needed, &hole_start, &hole_end);
  }

  // Still not enough contiguous space is available on disk, try shrinking STATE
  // Note STATE type GUID can either be cros_data or, in the case of a freshly
  // recovered device, linux_filesystem
  if (!found_hole && (((p = find_by_type_and_name(gpt, kStateCrosGuid, "STATE")) != NULL) ||
                      (p = find_by_type_and_name(gpt, kStateLinuxGuid, "STATE")) != NULL)) {
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
  uint64_t sz_syscfg_blks = howmany(kSysCfgSize, blk_info->block_size);
  if ((status = create_gpt_entry(gpt, hole_start, sz_syscfg_blks, kSysCfgGuid, "SYSCFG")) !=
      ZX_OK) {
    printf("cros-disk-setup: Error creating SYSCFG: %d\n", status);
    return ZX_ERR_INTERNAL;
  }
  hole_start += sz_syscfg_blks;

  uint64_t sz_kern_blks = howmany(sz_kern, blk_info->block_size);

  // Create GPT entries for ZIRCON-A, ZIRCON-B, ZIRCON-R and FVM if needed.
  const char* kernel_names[3] = {"ZIRCON-A", "ZIRCON-B", "ZIRCON-R"};
  for (size_t i = 0; i < 3; ++i) {
    printf("cros-disk-setup: Creating %s\n", kernel_names[i]);
    if ((status = create_gpt_entry(gpt, hole_start, sz_kern_blks, kKernGuid, kernel_names[i])) !=
        ZX_OK) {
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
  if ((status = create_gpt_entry(gpt, hole_start, (hole_end - hole_start), kFvmGuid, "fvm")) !=
      ZX_OK) {
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

__END_CDECLS

}  // namespace chromeos_disk_setup
