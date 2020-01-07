// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#pragma once

#include <fuchsia/hardware/block/llcpp/fidl.h>
#include <fuchsia/hardware/skipblock/llcpp/fidl.h>
#include <lib/sysconfig/sync-client.h>
#include <lib/zx/channel.h>
#include <lib/zx/vmo.h>
#include <zircon/types.h>

#include <optional>

#include <block-client/cpp/client.h>
#include <fbl/unique_fd.h>

namespace paver {

// Interface to synchronously read/write to a partition.
class PartitionClient {
 public:
  // Returns the block size which the vmo provided to read/write should be aligned to.
  virtual zx_status_t GetBlockSize(size_t* out_size) = 0;

  // Returns the partition size.
  virtual zx_status_t GetPartitionSize(size_t* out_size) = 0;

  // Reads the specified size from the partition into |vmo|. |size| must be aligned to the block
  // size returned in `GetBlockSize`.
  virtual zx_status_t Read(const zx::vmo& vmo, size_t size) = 0;

  // Writes |vmo| into the partition. |vmo_size| must be aligned to the block size returned in
  // `GetBlockSize`.
  virtual zx_status_t Write(const zx::vmo& vmo, size_t vmo_size) = 0;

  // Issues a trim to the entire partition.
  virtual zx_status_t Trim() = 0;

  // Flushes all previous operations to persistent storage.
  virtual zx_status_t Flush() = 0;

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

  zx_status_t GetBlockSize(size_t* out_size) final;
  zx_status_t GetPartitionSize(size_t* out_size) final;
  zx_status_t Read(const zx::vmo& vmo, size_t size) final;
  zx_status_t Write(const zx::vmo& vmo, size_t vmo_size) final;
  zx_status_t Trim() final;
  zx_status_t Flush() final;
  zx::channel GetChannel() final;
  fbl::unique_fd block_fd() final;

  // No copy, no move.
  BlockPartitionClient(const BlockPartitionClient&) = delete;
  BlockPartitionClient& operator=(const BlockPartitionClient&) = delete;
  BlockPartitionClient(BlockPartitionClient&&) = delete;
  BlockPartitionClient& operator=(BlockPartitionClient&&) = delete;

 private:
  zx_status_t Setup(const zx::vmo& vmo, vmoid_t* out_vmoid);
  zx_status_t RegisterFastBlockIo();
  zx_status_t RegisterVmo(const zx::vmo& vmo, vmoid_t* out_vmoid);
  zx_status_t ReadBlockInfo();

  ::llcpp::fuchsia::hardware::block::Block::SyncClient partition_;
  std::optional<block_client::Client> client_;
  std::optional<::llcpp::fuchsia::hardware::block::BlockInfo> block_info_;
};

class SkipBlockPartitionClient : public PartitionClient {
 public:
  explicit SkipBlockPartitionClient(zx::channel partition) : partition_(std::move(partition)) {}

  zx_status_t GetBlockSize(size_t* out_size) override;
  zx_status_t GetPartitionSize(size_t* out_size) override;
  zx_status_t Read(const zx::vmo& vmo, size_t size) override;
  zx_status_t Write(const zx::vmo& vmo, size_t vmo_size) override;
  zx_status_t Trim() override;
  zx_status_t Flush() override;
  zx::channel GetChannel() override;
  fbl::unique_fd block_fd() override;

  // No copy, no move.
  SkipBlockPartitionClient(const SkipBlockPartitionClient&) = delete;
  SkipBlockPartitionClient& operator=(const SkipBlockPartitionClient&) = delete;
  SkipBlockPartitionClient(SkipBlockPartitionClient&&) = delete;
  SkipBlockPartitionClient& operator=(SkipBlockPartitionClient&&) = delete;

 protected:
  zx_status_t WriteBytes(const zx::vmo& vmo, zx_off_t offset, size_t vmo_size);

 private:
  zx_status_t ReadPartitionInfo();

  ::llcpp::fuchsia::hardware::skipblock::SkipBlock::SyncClient partition_;
  std::optional<::llcpp::fuchsia::hardware::skipblock::PartitionInfo> partition_info_;
};

// Specialized client for talking to sub-partitions of the sysconfig partition.
class SysconfigPartitionClient final : public PartitionClient {
 public:
  SysconfigPartitionClient(::sysconfig::SyncClient client,
                           ::sysconfig::SyncClient::PartitionType partition)
      : client_(std::move(client)), partition_(partition) {}

  zx_status_t GetBlockSize(size_t* out_size) final;
  zx_status_t GetPartitionSize(size_t* out_size) final;
  zx_status_t Read(const zx::vmo& vmo, size_t size) final;
  zx_status_t Write(const zx::vmo& vmo, size_t vmo_size) final;
  zx_status_t Trim() final;
  zx_status_t Flush() final;
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

// Specialized partition client which duplciates to multiple partitions, and attempts to read from
// each.
class PartitionCopyClient final : public PartitionClient {
 public:
  explicit PartitionCopyClient(std::vector<std::unique_ptr<PartitionClient>> partitions)
      : partitions_(std::move(partitions)) {}

  zx_status_t GetBlockSize(size_t* out_size) final;
  zx_status_t GetPartitionSize(size_t* out_size) final;
  zx_status_t Read(const zx::vmo& vmo, size_t size) final;
  zx_status_t Write(const zx::vmo& vmo, size_t vmo_size) final;
  zx_status_t Trim() final;
  zx_status_t Flush() final;
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

  zx_status_t GetBlockSize(size_t* out_size) final;
  zx_status_t GetPartitionSize(size_t* out_size) final;
  zx_status_t Read(const zx::vmo& vmo, size_t size) final;
  zx_status_t Write(const zx::vmo& vmo, size_t vmo_size) final;

  // No copy, no move.
  Bl2PartitionClient(const Bl2PartitionClient&) = delete;
  Bl2PartitionClient& operator=(const Bl2PartitionClient&) = delete;
  Bl2PartitionClient(Bl2PartitionClient&&) = delete;
  Bl2PartitionClient& operator=(Bl2PartitionClient&&) = delete;

 private:
  static constexpr size_t kNandPageSize = 4 * 1024;
  static constexpr size_t kBl2Size = 64 * 1024;
};

class AstroBootloaderPartitionClient final : public PartitionClient {
 public:
  explicit AstroBootloaderPartitionClient(std::unique_ptr<PartitionClient> bl2,
                                          std::unique_ptr<PartitionClient> tpl)
      : bl2_(std::move(bl2)), tpl_(std::move(tpl)) {}

  zx_status_t GetBlockSize(size_t* out_size) final;
  zx_status_t GetPartitionSize(size_t* out_size) final;
  zx_status_t Read(const zx::vmo& vmo, size_t size) final;
  zx_status_t Write(const zx::vmo& vmo, size_t vmo_size) final;
  zx_status_t Trim() final;
  zx_status_t Flush() final;
  zx::channel GetChannel() final;
  fbl::unique_fd block_fd() final;

  // No copy, no move.
  AstroBootloaderPartitionClient(const AstroBootloaderPartitionClient&) = delete;
  AstroBootloaderPartitionClient& operator=(const AstroBootloaderPartitionClient&) = delete;
  AstroBootloaderPartitionClient(AstroBootloaderPartitionClient&&) = delete;
  AstroBootloaderPartitionClient& operator=(AstroBootloaderPartitionClient&&) = delete;

 private:
  std::unique_ptr<PartitionClient> bl2_;
  std::unique_ptr<PartitionClient> tpl_;
};

}  // namespace paver
