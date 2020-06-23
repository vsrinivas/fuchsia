// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef ZIRCON_SYSTEM_ULIB_PAVER_PARTITION_CLIENT_H_
#define ZIRCON_SYSTEM_ULIB_PAVER_PARTITION_CLIENT_H_

#include <fuchsia/hardware/block/llcpp/fidl.h>
#include <fuchsia/hardware/skipblock/llcpp/fidl.h>
#include <lib/sysconfig/sync-client.h>
#include <lib/zx/channel.h>
#include <lib/zx/status.h>
#include <lib/zx/vmo.h>
#include <zircon/types.h>

#include <optional>

#include <block-client/cpp/client.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <fbl/unique_fd.h>

#include "paver-context.h"

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

class SkipBlockPartitionClient : public PartitionClient {
 public:
  explicit SkipBlockPartitionClient(zx::channel partition) : partition_(std::move(partition)) {}

  zx::status<size_t> GetBlockSize() override;
  zx::status<size_t> GetPartitionSize() override;
  zx::status<> Read(const zx::vmo& vmo, size_t size) override;
  zx::status<> Write(const zx::vmo& vmo, size_t vmo_size) override;
  zx::status<> Trim() override;
  zx::status<> Flush() override;
  zx::channel GetChannel() override;
  fbl::unique_fd block_fd() override;

  // No copy, no move.
  SkipBlockPartitionClient(const SkipBlockPartitionClient&) = delete;
  SkipBlockPartitionClient& operator=(const SkipBlockPartitionClient&) = delete;
  SkipBlockPartitionClient(SkipBlockPartitionClient&&) = delete;
  SkipBlockPartitionClient& operator=(SkipBlockPartitionClient&&) = delete;

 protected:
  zx::status<> WriteBytes(const zx::vmo& vmo, zx_off_t offset, size_t vmo_size);

 private:
  zx::status<> ReadPartitionInfo();

  ::llcpp::fuchsia::hardware::skipblock::SkipBlock::SyncClient partition_;
  std::optional<::llcpp::fuchsia::hardware::skipblock::PartitionInfo> partition_info_;
};

// Specialized client for talking to sub-partitions of the sysconfig partition.
class SysconfigPartitionClient final : public PartitionClient {
 public:
  SysconfigPartitionClient(::sysconfig::SyncClient client,
                           ::sysconfig::SyncClient::PartitionType partition)
      : client_(std::move(client)), partition_(partition) {}

  zx::status<size_t> GetBlockSize() final;
  zx::status<size_t> GetPartitionSize() final;
  zx::status<> Read(const zx::vmo& vmo, size_t size) final;
  zx::status<> Write(const zx::vmo& vmo, size_t vmo_size) final;
  zx::status<> Trim() final;
  zx::status<> Flush() final;
  zx::channel GetChannel() final;
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

// Specialized astro sysconfig partition client built on SyncClientBuffered.
class AstroSysconfigPartitionClientBuffered : public PartitionClient {
 public:
  AstroSysconfigPartitionClientBuffered(std::shared_ptr<Context> context,
                                        ::sysconfig::SyncClient::PartitionType partition)
      : context_(context), partition_(partition) {}

  zx::status<size_t> GetBlockSize() final;
  zx::status<size_t> GetPartitionSize() final;
  zx::status<> Read(const zx::vmo& vmo, size_t size) final;
  zx::status<> Write(const zx::vmo& vmo, size_t vmo_size) final;
  zx::status<> Trim() final;
  zx::status<> Flush() final;
  zx::channel GetChannel() final;
  fbl::unique_fd block_fd() final;

  // No copy, no move.
  AstroSysconfigPartitionClientBuffered(const AstroSysconfigPartitionClientBuffered&) = delete;
  AstroSysconfigPartitionClientBuffered& operator=(const AstroSysconfigPartitionClientBuffered&) =
      delete;
  AstroSysconfigPartitionClientBuffered(AstroSysconfigPartitionClientBuffered&&) = delete;
  AstroSysconfigPartitionClientBuffered& operator=(AstroSysconfigPartitionClientBuffered&&) =
      delete;

 private:
  std::shared_ptr<Context> context_;
  ::sysconfig::SyncClient::PartitionType partition_;
};

// Specialized partition client which duplciates to multiple partitions, and attempts to read from
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

// Specialized layer on top of SkipBlockPartitionClient to deal with page0 quirk and block size
// quirk.
class Bl2PartitionClient final : public SkipBlockPartitionClient {
 public:
  explicit Bl2PartitionClient(zx::channel partition)
      : SkipBlockPartitionClient(std::move(partition)) {}

  zx::status<size_t> GetBlockSize() final;
  zx::status<size_t> GetPartitionSize() final;
  zx::status<> Read(const zx::vmo& vmo, size_t size) final;
  zx::status<> Write(const zx::vmo& vmo, size_t vmo_size) final;

  // No copy, no move.
  Bl2PartitionClient(const Bl2PartitionClient&) = delete;
  Bl2PartitionClient& operator=(const Bl2PartitionClient&) = delete;
  Bl2PartitionClient(Bl2PartitionClient&&) = delete;
  Bl2PartitionClient& operator=(Bl2PartitionClient&&) = delete;

 private:
  static constexpr size_t kNandPageSize = 4 * 1024;
  static constexpr size_t kBl2Size = 64 * 1024;
};

class SherlockBootloaderPartitionClient final : public PartitionClient {
 public:
  explicit SherlockBootloaderPartitionClient(zx::channel partition)
      : client_(std::move(partition)) {}

  zx::status<size_t> GetBlockSize() final;
  zx::status<size_t> GetPartitionSize() final;
  zx::status<> Read(const zx::vmo& vmo, size_t size) final;
  zx::status<> Write(const zx::vmo& vmo, size_t vmo_size) final;
  zx::status<> Trim() final;
  zx::status<> Flush() final;
  zx::channel GetChannel() final;
  fbl::unique_fd block_fd() final;

  // No copy, no move.
  SherlockBootloaderPartitionClient(const SherlockBootloaderPartitionClient&) = delete;
  SherlockBootloaderPartitionClient& operator=(const SherlockBootloaderPartitionClient&) = delete;
  SherlockBootloaderPartitionClient(SherlockBootloaderPartitionClient&&) = delete;
  SherlockBootloaderPartitionClient& operator=(SherlockBootloaderPartitionClient&&) = delete;

 private:
  BlockPartitionClient client_;
};

}  // namespace paver

#endif  // ZIRCON_SYSTEM_ULIB_PAVER_PARTITION_CLIENT_H_
