// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file describes the in-memory structures which construct
// a F2FS filesystem.

#ifndef SRC_STORAGE_F2FS_BCACHE_H_
#define SRC_STORAGE_F2FS_BCACHE_H_

#ifdef __Fuchsia__
#include <lib/zx/vmo.h>

#include <storage/buffer/vmo_buffer.h>
#include <storage/buffer/vmoid_registry.h>

#include "src/lib/storage/block_client/cpp/block_device.h"
#include "src/lib/storage/block_client/cpp/client.h"
#endif  // __Fuchsia__

#include <errno.h>
#include <inttypes.h>

#include <atomic>
#include <shared_mutex>

#include <fbl/algorithm.h>
#include <fbl/array.h>
#include <fbl/macros.h>
#include <fbl/unique_fd.h>

#ifdef __Fuchsia__
#include "src/lib/storage/vfs/cpp/transaction/device_transaction_handler.h"
#else
#include "src/lib/storage/vfs/cpp/transaction/transaction_handler.h"
#endif  // __Fuchsia__

namespace f2fs {

#ifdef __Fuchsia__
class Bcache : public fs::DeviceTransactionHandler, public storage::VmoidRegistry {
#else   // __Fuchsia__
class Bcache : public fs::TransactionHandler {
#endif  // __Fuchsia__
 public:
  // Not copyable or movable
  Bcache(const Bcache&) = delete;
  Bcache& operator=(const Bcache&) = delete;
  Bcache(Bcache&&) = delete;
  Bcache& operator=(Bcache&&) = delete;

  // Destroys a "bcache" object, but take back ownership of the underlying block device.
#ifdef __Fuchsia__
  static std::unique_ptr<block_client::BlockDevice> Destroy(std::unique_ptr<Bcache> bcache);
#endif  // __Fuchsia__

  ////////////////
  // fs::TransactionHandler interface.
#ifdef __Fuchsia__
  zx_status_t RunRequests(const std::vector<storage::BufferedOperation>& operations) override {
    std::shared_lock lock(mutex_);
    return DeviceTransactionHandler::RunRequests(operations);
  }
#else   // __Fuchsia__
  zx_status_t RunOperation(const storage::Operation& operation,
                           storage::BlockBuffer* buffer) override {
    return TransactionHandler::RunOperation(operation, buffer);
  }

  zx_status_t RunRequests(const std::vector<storage::BufferedOperation>& operations) override {
    return ZX_OK;
  }
#endif  // __Fuchsia__

  uint64_t BlockNumberToDevice(uint64_t block_num) const final {
#ifdef __Fuchsia__
    return block_num * BlockSize() / info_.block_size;
#else   // __Fuchsia__
    return block_num * kBlockSize / kDefaultSectorSize;
#endif  // __Fuchsia__
  }

#ifdef __Fuchsia__
  block_client::BlockDevice* GetDevice() final { return device_; }
#endif  // __Fuchsia__

  uint64_t DeviceBlockSize() const;

  // Raw block read/write/trim functions
  // |bno| is a target LBA in a 4KB block size
  // These do not track blocks (or attempt to access the block cache)
  // NOTE: Not marked as final, since these are overridden methods on host,
  // but not on __Fuchsia__.
  zx_status_t Readblk(block_t bno, void* data);
  zx_status_t Writeblk(block_t bno, const void* data);
  zx_status_t Trim(block_t start, block_t num);
#ifdef __Fuchsia__
  zx_status_t Flush() override { return DeviceTransactionHandler::Flush(); }
#else   // __Fuchsia__
  zx_status_t Flush() override { return TransactionHandler::Flush(); }
#endif  // __Fuchsia__

#ifdef __Fuchsia__
  zx_status_t BlockAttachVmo(const zx::vmo& vmo, storage::Vmoid* out) final;
  zx_status_t BlockDetachVmo(storage::Vmoid vmoid) final;
#endif  // __Fuchsia__

  ////////////////
  // Other methods.

  // This factory allows building this object from a BlockDevice. Bcache can take ownership of the
  // device (the first Create method), or not (the second Create method).
#ifdef __Fuchsia__
  static zx_status_t Create(std::unique_ptr<block_client::BlockDevice> device, uint64_t max_blocks,
                            block_t block_size, std::unique_ptr<Bcache>* out);

  static zx_status_t Create(block_client::BlockDevice* device, uint64_t max_blocks,
                            block_t block_size, std::unique_ptr<Bcache>* out);
#else   // __Fuchsia__
  static zx_status_t Create(fbl::unique_fd fd, uint64_t max_blocks, std::unique_ptr<Bcache>* out);
#endif  // __Fuchsia__

  uint64_t Maxblk() const { return max_blocks_; }

#ifdef __Fuchsia__
  block_t BlockSize() const { return block_size_; }

  block_client::BlockDevice* device() { return device_; }
  const block_client::BlockDevice* device() const { return device_; }
#endif  // __Fuchsia__

  // Blocks all I/O operations to the underlying device (that go via the RunRequests method). This
  // does *not* block operations that go directly to the device.
  void Pause();

  // Resumes all I/O operations paused by the Pause method.
  void Resume();

 private:
  friend class BlockNode;

#ifdef __Fuchsia__
  Bcache(block_client::BlockDevice* device, uint64_t max_blocks, block_t block_size);
#else   // __Fuchsia__
  Bcache(fbl::unique_fd fd, uint64_t max_blocks);
#endif  // __Fuchsia__

  // Used during initialization of this object.
  zx_status_t VerifyDeviceInfo();

  const uint64_t max_blocks_;
#ifdef __Fuchsia__
  const block_t block_size_;
  fuchsia_hardware_block_BlockInfo info_ = {};
  std::unique_ptr<block_client::BlockDevice> owned_device_;  // The device, if owned.
  block_client::BlockDevice* device_;  // Pointer to the device, irrespective of ownership.
  // This buffer is used as internal scratch space for the "Readblk/Writeblk" methods.
  storage::VmoBuffer buffer_;
  std::shared_mutex mutex_;
#else   // __Fuchsia__
  const fbl::unique_fd fd_;
#endif  // __Fuchsia__
};

#ifdef __Fuchsia__
zx_status_t CreateBcache(std::unique_ptr<block_client::BlockDevice> device, bool* out_readonly,
                         std::unique_ptr<Bcache>* out);
#endif  // __Fuchsia__

}  // namespace f2fs

#endif  // SRC_STORAGE_F2FS_BCACHE_H_
