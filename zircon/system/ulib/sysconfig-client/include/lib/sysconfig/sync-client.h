// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_SYSCONFIG_SYNC_CLIENT_H_
#define LIB_SYSCONFIG_SYNC_CLIENT_H_

#include <fuchsia/hardware/skipblock/llcpp/fidl.h>
#include <lib/fzl/owned-vmo-mapper.h>
#include <lib/zx/channel.h>
#include <zircon/types.h>

#include <optional>

#include <fbl/unique_fd.h>

#include "sysconfig-header.h"

namespace sysconfig {

// This class provides a synchronous read and write interface into sub-partitions of the sysconfig
// skip-block partition.
//
// The class takes into account differences that may appear in partition layout between various
// device's sysconfig partitions.
class __EXPORT SyncClient {
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
  zx_status_t GetPartitionSize(PartitionType partition, size_t* out);

  zx_status_t GetPartitionOffset(PartitionType partition, size_t* out);

  // Use caution when updating layout in multi-threaded context.
  // It's dangerous to update layout and header while there are other instances of SyncClient
  // in use.
  // In particular, SyncClient caches header from storage after the first time it reads it. If
  // layout is changed afterwards by some other instance of SyncClient, it will not be aware of it.
  // Thus make sure that you only effectively update layout in a state where no other SyncClient is
  // created and in use.
  zx_status_t UpdateLayout(const sysconfig_header& target_header);

  // No copy.
  SyncClient(const SyncClient&) = delete;
  SyncClient& operator=(const SyncClient&) = delete;

  SyncClient(SyncClient&&) = default;
  SyncClient& operator=(SyncClient&&) = default;

  const sysconfig_header* GetHeader(zx_status_t* status_out = nullptr);

 private:
  SyncClient(::llcpp::fuchsia::hardware::skipblock::SkipBlock::SyncClient skip_block)
      : skip_block_(std::move(skip_block)) {}

  zx_status_t InitializeReadMapper();

  zx_status_t Write(size_t offset, size_t len, const zx::vmo& vmo, zx_off_t vmo_offset);

  zx_status_t Read(size_t offset, size_t len, const zx::vmo& vmo, zx_off_t vmo_offset);

  zx_status_t LoadFromStorage();

  ::llcpp::fuchsia::hardware::skipblock::SkipBlock::SyncClient skip_block_;

  // Lazily initialized on reads.
  fzl::OwnedVmoMapper read_mapper_;

  // Once loaded from storage, the header will be cached here
  std::unique_ptr<sysconfig_header> header_;
};

}  // namespace sysconfig

#endif  // LIB_SYSCONFIG_SYNC_CLIENT_H_
