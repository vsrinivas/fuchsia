// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_STORAGE_LIB_PAVER_PARTITION_CLIENT_H_
#define SRC_STORAGE_LIB_PAVER_PARTITION_CLIENT_H_

#include <fidl/fuchsia.hardware.block.partition/cpp/wire.h>
#include <fidl/fuchsia.hardware.block/cpp/wire.h>
#include <lib/zx/channel.h>
#include <lib/zx/result.h>
#include <lib/zx/vmo.h>
#include <zircon/types.h>

#include <optional>
#include <vector>

#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <fbl/unique_fd.h>

#include "src/lib/storage/block_client/cpp/client.h"

namespace paver {

// Interface to synchronously read/write to a partition.
class PartitionClient {
 public:
  // Returns the block size which the vmo provided to read/write should be aligned to.
  virtual zx::result<size_t> GetBlockSize() = 0;

  // Returns the partition size.
  virtual zx::result<size_t> GetPartitionSize() = 0;

  // Reads the specified size from the partition into |vmo|. |size| must be aligned to the block
  // size returned in `GetBlockSize`.
  virtual zx::result<> Read(const zx::vmo& vmo, size_t size) = 0;

  // Writes |vmo| into the partition. |vmo_size| must be aligned to the block size returned in
  // `GetBlockSize`.
  virtual zx::result<> Write(const zx::vmo& vmo, size_t vmo_size) = 0;

  // Issues a trim to the entire partition.
  virtual zx::result<> Trim() = 0;

  // Flushes all previous operations to persistent storage.
  virtual zx::result<> Flush() = 0;

  // Returns a file descriptor representing the partition.
  // Will return an invalid fd if underlying partition is not a block device.
  virtual fbl::unique_fd block_fd() = 0;

  virtual ~PartitionClient() = default;
};

// A partition client that is backed by a channel that speaks
// |fuchsia.hardware.block/Block|, or a protocol that composes the previous protocol.
class BlockDevicePartitionClient : public PartitionClient {
 public:
  // Returns a channel to the partition, when backed by a block device.
  virtual fidl::ClientEnd<fuchsia_hardware_block::Block> GetChannel() = 0;
};

class BlockPartitionClient final : public BlockDevicePartitionClient {
 public:
  explicit BlockPartitionClient(fidl::ClientEnd<fuchsia_hardware_block::Block> partition)
      : partition_(std::move(partition)) {}

  // Note: converting from |fuchsia.hardware.block.partition/Partition|
  // to |fuchsia.hardware.block/Block|.
  explicit BlockPartitionClient(
      fidl::ClientEnd<fuchsia_hardware_block_partition::Partition> partition)
      : BlockPartitionClient(
            fidl::ClientEnd<fuchsia_hardware_block::Block>(partition.TakeChannel())) {}

  ~BlockPartitionClient();

  zx::result<size_t> GetBlockSize() final;
  zx::result<size_t> GetPartitionSize() final;
  zx::result<> Read(const zx::vmo& vmo, size_t size) final;
  zx::result<> Read(const zx::vmo& vmo, size_t size, size_t dev_offset, size_t vmo_offset);
  zx::result<> Write(const zx::vmo& vmo, size_t vmo_size) final;
  zx::result<> Write(const zx::vmo& vmo, size_t vmo_size, size_t dev_offset, size_t vmo_offset);
  zx::result<> Trim() final;
  zx::result<> Flush() final;
  fidl::ClientEnd<fuchsia_hardware_block::Block> GetChannel() final;
  fbl::unique_fd block_fd() final;

  // No copy, no move.
  BlockPartitionClient(const BlockPartitionClient&) = delete;
  BlockPartitionClient& operator=(const BlockPartitionClient&) = delete;
  BlockPartitionClient(BlockPartitionClient&&) = delete;
  BlockPartitionClient& operator=(BlockPartitionClient&&) = delete;

 private:
  zx::result<vmoid_t> Setup(const zx::vmo& vmo);
  zx::result<> RegisterFastBlockIo();
  zx::result<vmoid_t> RegisterVmo(const zx::vmo& vmo);
  zx::result<> ReadBlockInfo();

  fidl::WireSyncClient<fuchsia_hardware_block::Block> partition_;
  std::unique_ptr<block_client::Client> client_;
  std::optional<fuchsia_hardware_block::wire::BlockInfo> block_info_;
};

// A variant of BlockPartitionClient that reads/writes starting from a fixed offset in
// the partition and from a fixed offset in the given buffer.
// This is for those cases where image doesn't necessarily start from the beginning of
// the partition, (i.e. for preserving metatdata/header).
// It's also used for cases where input image is a combined image for multiple partitions.
class FixedOffsetBlockPartitionClient final : public BlockDevicePartitionClient {
 public:
  explicit FixedOffsetBlockPartitionClient(fidl::ClientEnd<fuchsia_hardware_block::Block> partition,
                                           size_t offset_partition_in_blocks,
                                           size_t offset_buffer_in_blocks)
      : client_(std::move(partition)),
        offset_partition_in_blocks_(offset_partition_in_blocks),
        offset_buffer_in_blocks_(offset_buffer_in_blocks) {}

  // Note: converting from |fuchsia.hardware.block.partition/Partition|
  // to |fuchsia.hardware.block/Block|.
  explicit FixedOffsetBlockPartitionClient(
      fidl::ClientEnd<fuchsia_hardware_block_partition::Partition> partition,
      size_t offset_partition_in_blocks, size_t offset_buffer_in_blocks)
      : FixedOffsetBlockPartitionClient(
            fidl::ClientEnd<fuchsia_hardware_block::Block>(partition.TakeChannel()),
            offset_partition_in_blocks, offset_buffer_in_blocks) {}

  zx::result<size_t> GetBlockSize() final;
  zx::result<size_t> GetPartitionSize() final;
  zx::result<> Read(const zx::vmo& vmo, size_t size) final;
  zx::result<> Write(const zx::vmo& vmo, size_t vmo_size) final;
  zx::result<> Trim() final;
  zx::result<> Flush() final;
  fidl::ClientEnd<fuchsia_hardware_block::Block> GetChannel() final;
  fbl::unique_fd block_fd() final;

  // No copy, no move.
  FixedOffsetBlockPartitionClient(const FixedOffsetBlockPartitionClient&) = delete;
  FixedOffsetBlockPartitionClient& operator=(const FixedOffsetBlockPartitionClient&) = delete;
  FixedOffsetBlockPartitionClient(FixedOffsetBlockPartitionClient&&) = delete;
  FixedOffsetBlockPartitionClient& operator=(FixedOffsetBlockPartitionClient&&) = delete;

  zx::result<size_t> GetBufferOffsetInBytes();

 private:
  BlockPartitionClient client_;
  // offset in blocks for partition
  size_t offset_partition_in_blocks_ = 0;
  // offset in blocks for the input buffer
  size_t offset_buffer_in_blocks_ = 0;
};

// Specialized partition client which duplicates to multiple partitions, and attempts to read from
// each.
class PartitionCopyClient final : public BlockDevicePartitionClient {
 public:
  explicit PartitionCopyClient(std::vector<std::unique_ptr<PartitionClient>> partitions)
      : partitions_(std::move(partitions)) {}

  zx::result<size_t> GetBlockSize() final;
  zx::result<size_t> GetPartitionSize() final;
  zx::result<> Read(const zx::vmo& vmo, size_t size) final;
  zx::result<> Write(const zx::vmo& vmo, size_t vmo_size) final;
  zx::result<> Trim() final;
  zx::result<> Flush() final;
  fidl::ClientEnd<fuchsia_hardware_block::Block> GetChannel() final;
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
