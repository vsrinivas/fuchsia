// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOCK_DRIVERS_MBR_MBR_H_
#define SRC_STORAGE_BLOCK_DRIVERS_MBR_MBR_H_

#include <inttypes.h>
#include <zircon/status.h>

#include <cstddef>

namespace mbr {

static constexpr size_t kMbrSize = 512;
static constexpr size_t kMbrPartitionEntrySize = 16;

static constexpr size_t kMbrNumPartitions = 4;

static constexpr uint16_t kMbrBootSignature = 0xAA55;

static constexpr uint8_t kPartitionTypeNone = 0x00;
static constexpr uint8_t kPartitionTypeFuchsiaData = 0xE9;
static constexpr uint8_t kPartitionTypeFuchsiaSys = 0xEA;

struct MbrPartitionEntry {
  // 0x80 indicates active/bootable. 0x00 indicates inactive. All other values
  // indicate an invalid partition.
  uint8_t status;
  // Cylinder-Head-Sector address of first sector in partition. Generally
  // unused in favor of |start_sector_lba|.
  uint8_t chs_address_start[3];
  // Partition type.
  uint8_t type;
  // Cylinder-Head-Sector address of last sector in partition. Generally
  // unused in favor of |start_sector_lba| and |sector_partition_length|.
  uint8_t chs_address_end[3];
  // Logical Block Address of the first sector in the partition.
  uint32_t start_sector_lba;
  // Number of sectors in the partition.
  uint32_t num_sectors;
} __PACKED;

struct Mbr {
  uint8_t bootstrap_code[446];
  MbrPartitionEntry partitions[kMbrNumPartitions];
  uint16_t boot_signature = kMbrBootSignature;

  static zx_status_t Parse(const uint8_t* buffer, size_t bufsz, Mbr* out);
} __PACKED;

static_assert(sizeof(Mbr) == kMbrSize, "mbr::Mbr is the wrong size");
static_assert(sizeof(MbrPartitionEntry) == kMbrPartitionEntrySize,
              "mbr::MbrPartitionEntry is the wrong size");

}  // namespace mbr

#endif  // SRC_STORAGE_BLOCK_DRIVERS_MBR_MBR_H_
