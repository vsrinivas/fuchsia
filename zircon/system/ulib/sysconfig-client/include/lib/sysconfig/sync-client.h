// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fuchsia/hardware/skipblock/llcpp/fidl.h>
#include <lib/fzl/owned-vmo-mapper.h>
#include <lib/zx/channel.h>
#include <zircon/types.h>

#include <optional>

#include <fbl/unique_fd.h>

namespace sysconfig {

// This class provides a synchronous read and write interface into sub-partitions of the sysconfig
// skip-block partition.
//
// The class takes into account differences that may appear in partition layout between various
// device's sysconfig partitions.
class SyncClient {
 public:
  // The sub partitions of the sysconfig partition.
  enum class PartitionType {
    kSysconfig,
    // Used to determine which partition to boot into on boot.
    kABRMetadata,
    // The follow are used to store verified boot metadata.
    kVerifiedBootMetadataA,
    kVerifiedBootMetadataB,
    kVerifiedBootMetadataR,
  };

  // Looks for a skip-block device of type sysconfig. If found, returns a client capable of reading
  // and writing to sub-partitions of the sysconfig device.
  static zx_status_t Create(std::optional<SyncClient>* out);

  // Variation on `Create` with devfs (/dev) injected.
  static zx_status_t Create(const fbl::unique_fd& devfs_root, std::optional<SyncClient>* out);

  // Provides write access for the partition specified. Always writes full partition.
  //
  // |vmo| must have a size greater than or equal to the partitions size + |vmo_offset|.
  zx_status_t WritePartition(PartitionType partition, const zx::vmo& vmo, zx_off_t vmo_offset);

  // Provides read access for the partition specified. Always reads full partition.
  //
  // |vmo| must have a size greater than or equal to the partitions size + |vmo_offset|.
  zx_status_t ReadPartition(PartitionType partition, const zx::vmo& vmo, zx_off_t vmo_offset);

  // Returns the size of the partition specified.
  size_t GetPartitionSize(PartitionType partition);

  // No copy.
  SyncClient(const SyncClient&) = delete;
  SyncClient& operator=(const SyncClient&) = delete;

  SyncClient(SyncClient&&) = default;
  SyncClient& operator=(SyncClient&&) = default;

 private:
  SyncClient(::llcpp::fuchsia::hardware::skipblock::SkipBlock::SyncClient skip_block)
      : skip_block_(std::move(skip_block)) {}

  zx_status_t InitializeReadMapper();
  size_t GetPartitionOffset(PartitionType partition);

  ::llcpp::fuchsia::hardware::skipblock::SkipBlock::SyncClient skip_block_;

  // Lazily initialized on reads.
  fzl::OwnedVmoMapper read_mapper_;
};

}  // namespace sysconfig
