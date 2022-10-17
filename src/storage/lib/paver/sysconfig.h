// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_STORAGE_LIB_PAVER_SYSCONFIG_H_
#define SRC_STORAGE_LIB_PAVER_SYSCONFIG_H_

#include <lib/sysconfig/sync-client.h>

#include "src/storage/lib/paver/partition-client.h"

namespace paver {

// Specialized client for talking to sub-partitions of the sysconfig partition.
class SysconfigPartitionClient final : public BlockDevicePartitionClient {
 public:
  SysconfigPartitionClient(::sysconfig::SyncClient client,
                           ::sysconfig::SyncClient::PartitionType partition)
      : client_(std::move(client)), partition_(partition) {}

  zx::result<size_t> GetBlockSize() final;
  zx::result<size_t> GetPartitionSize() final;
  zx::result<> Read(const zx::vmo& vmo, size_t size) final;
  zx::result<> Write(const zx::vmo& vmo, size_t vmo_size) final;
  zx::result<> Trim() final;
  zx::result<> Flush() final;
  fidl::ClientEnd<fuchsia_hardware_block::Block> GetChannel() final;
  fbl::unique_fd block_fd() final;

  // No copy, no move.
  SysconfigPartitionClient(const SysconfigPartitionClient&) = delete;
  SysconfigPartitionClient& operator=(const SysconfigPartitionClient&) = delete;
  SysconfigPartitionClient(SysconfigPartitionClient&&) = delete;
  SysconfigPartitionClient& operator=(SysconfigPartitionClient&&) = delete;

 private:
  ::sysconfig::SyncClient client_;
  ::sysconfig::SyncClient::PartitionType partition_;
};

}  // namespace paver

#endif  // SRC_STORAGE_LIB_PAVER_SYSCONFIG_H_
