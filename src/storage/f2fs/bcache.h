// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file describes the in-memory structures which construct
// a F2FS filesystem.

#ifndef SRC_STORAGE_F2FS_BCACHE_H_
#define SRC_STORAGE_F2FS_BCACHE_H_

#include <errno.h>
#include <fuchsia/hardware/block/c/fidl.h>
#include <inttypes.h>
#include <lib/zx/vmo.h>

#include <atomic>
#include <shared_mutex>

#include <block-client/cpp/block-device.h>
#include <block-client/cpp/client.h>
#include <fbl/algorithm.h>
#include <fbl/array.h>
#include <fbl/macros.h>
#include <fbl/unique_fd.h>
#include <storage/buffer/vmo_buffer.h>
#include <storage/buffer/vmoid_registry.h>

#include "src/lib/storage/vfs/cpp/transaction/device_transaction_handler.h"

namespace f2fs {

class Bcache : public fs::DeviceTransactionHandler, public storage::VmoidRegistry {
 public:
  // Not copyable or movable
  Bcache(const Bcache&) = delete;
  Bcache& operator=(const Bcache&) = delete;
  Bcache(Bcache&&) = delete;
  Bcache& operator=(Bcache&&) = delete;

  ~Bcache() = default;

  // Destroys a "bcache" object, but take back ownership of the underlying block device.
  static std::unique_ptr<block_client::BlockDevice> Destroy(std::unique_ptr<Bcache> bcache);

  ////////////////
  // fs::TransactionHandler interface.

  zx_status_t RunRequests(const std::vector<storage::BufferedOperation>& operations) override {
    std::shared_lock lock(mutex_);
    return DeviceTransactionHandler::RunRequests(operations);
  }

  uint64_t BlockNumberToDevice(uint64_t block_num) const final {
    return block_num * BlockSize() / info_.block_size;
  }

  block_client::BlockDevice* GetDevice() final { return device_; }

  uint64_t DeviceBlockSize() const;

  // Raw block read/write/trim functions
  // |bno| is a target LBA in a 4KB block size
  // TODO: create io functions with vmo and request for removing memory copy
  // These do not track blocks (or attempt to access the block cache)
  // NOTE: Not marked as final, since these are overridden methods on host,
  // but not on __Fuchsia__.
  zx_status_t Readblk(block_t bno, void* data);
  zx_status_t Writeblk(block_t bno, const void* data);
  zx_status_t Trim(block_t start, block_t num);
  zx_status_t Flush() override { return DeviceTransactionHandler::Flush(); }

  // TODO(rvargas): Move this to BlockDevice.
  // VmoidRegistry interface:
  zx_status_t BlockAttachVmo(const zx::vmo& vmo, storage::Vmoid* out) final;
  zx_status_t BlockDetachVmo(storage::Vmoid vmoid) final;

  ////////////////
  // Other methods.

  // This factory allows building this object from a BlockDevice. Bcache can take ownership of the
  // device (the first Create method), or not (the second Create method).
  static zx_status_t Create(std::unique_ptr<block_client::BlockDevice> device, uint64_t max_blocks,
                            uint64_t block_size, std::unique_ptr<Bcache>* out);

  static zx_status_t Create(block_client::BlockDevice* device, uint64_t max_blocks,
                            uint64_t block_size, std::unique_ptr<Bcache>* out);

  uint64_t Maxblk() const { return max_blocks_; }
  uint64_t BlockSize() const { return block_size_; }

  block_client::BlockDevice* device() { return device_; }
  const block_client::BlockDevice* device() const { return device_; }

  // Blocks all I/O operations to the underlying device (that go via the RunRequests method). This
  // does *not* block operations that go directly to the device.
  void Pause();

  // Resumes all I/O operations paused by the Pause method.
  void Resume();

 private:
  friend class BlockNode;

  Bcache(block_client::BlockDevice* device, uint64_t max_blocks, uint64_t block_size);

  // Used during initialization of this object.
  zx_status_t VerifyDeviceInfo();

  const uint64_t max_blocks_;
  const uint64_t block_size_;
  fuchsia_hardware_block_BlockInfo info_ = {};
  std::unique_ptr<block_client::BlockDevice> owned_device_;  // The device, if owned.
  block_client::BlockDevice* device_;  // Pointer to the device, irrespective of ownership.
  // This buffer is used as internal scratch space for the "Readblk/Writeblk" methods.
  storage::VmoBuffer buffer_;
  std::shared_mutex mutex_;
};

zx_status_t CreateBcache(std::unique_ptr<block_client::BlockDevice> device, bool* out_readonly,
                         std::unique_ptr<Bcache>* out);

}  // namespace f2fs

#endif  // SRC_STORAGE_F2FS_BCACHE_H_
