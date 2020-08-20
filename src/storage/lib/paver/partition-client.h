// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_STORAGE_LIB_PAVER_PARTITION_CLIENT_H_
#define SRC_STORAGE_LIB_PAVER_PARTITION_CLIENT_H_

#include <fuchsia/hardware/block/llcpp/fidl.h>
#include <lib/zx/channel.h>
#include <lib/zx/status.h>
#include <lib/zx/vmo.h>
#include <zircon/types.h>

#include <optional>

#include <block-client/cpp/client.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <fbl/unique_fd.h>

namespace paver {

// Interface to synchronously read/write to a partition.
class PartitionClient {
 public:
  // Returns the block size which the vmo provided to read/write should be aligned to.
  virtual zx::status<size_t> GetBlockSize() = 0;

  // Returns the partition size.
  virtual zx::status<size_t> GetPartitionSize() = 0;

  // Reads the specified size from the partition into |vmo|. |size| must be aligned to the block
  // size returned in `GetBlockSize`.
  virtual zx::status<> Read(const zx::vmo& vmo, size_t size) = 0;

  // Writes |vmo| into the partition. |vmo_size| must be aligned to the block size returned in
  // `GetBlockSize`.
  virtual zx::status<> Write(const zx::vmo& vmo, size_t vmo_size) = 0;

  // Issues a trim to the entire partition.
  virtual zx::status<> Trim() = 0;

  // Flushes all previous operations to persistent storage.
  virtual zx::status<> Flush() = 0;

  // Returns a channel to the partition, when backed by a block device.
  virtual zx::channel GetChannel() = 0;

  // Returns a file descriptor representing the partition.
  // Will return an invalid fd if underlying partition is not a block device.
  virtual fbl::unique_fd block_fd() = 0;

  virtual ~PartitionClient() = default;
};

class BlockPartitionClient final : public PartitionClient {
 public:
  explicit BlockPartitionClient(zx::channel partition) : partition_(std::move(partition)) {}

  ~BlockPartitionClient();

  zx::status<size_t> GetBlockSize() final;
  zx::status<size_t> GetPartitionSize() final;
  zx::status<> Read(const zx::vmo& vmo, size_t size) final;
  zx::status<> Read(const zx::vmo& vmo, size_t size, size_t dev_offset);
  zx::status<> Write(const zx::vmo& vmo, size_t vmo_size) final;
  zx::status<> Write(const zx::vmo& vmo, size_t vmo_size, size_t dev_offset);
  zx::status<> Trim() final;
  zx::status<> Flush() final;
  zx::channel GetChannel() final;
  fbl::unique_fd block_fd() final;

  // No copy, no move.
  BlockPartitionClient(const BlockPartitionClient&) = delete;
  BlockPartitionClient& operator=(const BlockPartitionClient&) = delete;
  BlockPartitionClient(BlockPartitionClient&&) = delete;
  BlockPartitionClient& operator=(BlockPartitionClient&&) = delete;

 private:
  zx::status<vmoid_t> Setup(const zx::vmo& vmo);
  zx::status<> RegisterFastBlockIo();
  zx::status<vmoid_t> RegisterVmo(const zx::vmo& vmo);
  zx::status<> ReadBlockInfo();

  ::llcpp::fuchsia::hardware::block::Block::SyncClient partition_;
  std::optional<block_client::Client> client_;
  std::optional<::llcpp::fuchsia::hardware::block::BlockInfo> block_info_;
};

// A variant of BlockPartitionClient that writes starting from a fixed offset.
// This is for those cases where image doesn't necessarily start from the beginning of
// the partition, (i.e. for preserving metatdata/header).
class FixedOffsetBlockPartitionClient final : public PartitionClient {
 public:
  explicit FixedOffsetBlockPartitionClient(zx::channel partition, size_t offset_in_blocks)
      : client_(std::move(partition)), offset_in_blocks_(offset_in_blocks) {}

  zx::status<size_t> GetBlockSize() final;
  zx::status<size_t> GetPartitionSize() final;
  zx::status<> Read(const zx::vmo& vmo, size_t size) final;
  zx::status<> Write(const zx::vmo& vmo, size_t vmo_size) final;
  zx::status<> Trim() final;
  zx::status<> Flush() final;
  zx::channel GetChannel() final;
  fbl::unique_fd block_fd() final;

  // No copy, no move.
  FixedOffsetBlockPartitionClient(const FixedOffsetBlockPartitionClient&) = delete;
  FixedOffsetBlockPartitionClient& operator=(const FixedOffsetBlockPartitionClient&) = delete;
  FixedOffsetBlockPartitionClient(FixedOffsetBlockPartitionClient&&) = delete;
  FixedOffsetBlockPartitionClient& operator=(FixedOffsetBlockPartitionClient&&) = delete;

 private:
  BlockPartitionClient client_;
  // offset in blocks
  size_t offset_in_blocks_ = 0;
};

// Specialized partition client which duplicates to multiple partitions, and attempts to read from
// each.
class PartitionCopyClient final : public PartitionClient {
 public:
  explicit PartitionCopyClient(std::vector<std::unique_ptr<PartitionClient>> partitions)
      : partitions_(std::move(partitions)) {}

  zx::status<size_t> GetBlockSize() final;
  zx::status<size_t> GetPartitionSize() final;
  zx::status<> Read(const zx::vmo& vmo, size_t size) final;
  zx::status<> Write(const zx::vmo& vmo, size_t vmo_size) final;
  zx::status<> Trim() final;
  zx::status<> Flush() final;
  zx::channel GetChannel() final;
  fbl::unique_fd block_fd() final;

  // No copy, no move.
  PartitionCopyClient(const PartitionCopyClient&) = delete;
  PartitionCopyClient& operator=(const PartitionCopyClient&) = delete;
  PartitionCopyClient(PartitionCopyClient&&) = delete;
  PartitionCopyClient& operator=(PartitionCopyClient&&) = delete;

 private:
  std::vector<std::unique_ptr<PartitionClient>> partitions_;
};

}  // namespace paver

#endif  // SRC_STORAGE_LIB_PAVER_PARTITION_CLIENT_H_
