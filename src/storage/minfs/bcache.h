// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file describes the in-memory structures which construct
// a MinFS filesystem.

#ifndef SRC_STORAGE_MINFS_BCACHE_H_
#define SRC_STORAGE_MINFS_BCACHE_H_

#include <errno.h>
#include <inttypes.h>

#include <atomic>
#include <shared_mutex>

#include <fbl/algorithm.h>
#include <fbl/array.h>
#include <fbl/macros.h>
#include <fbl/unique_fd.h>

#include "src/storage/minfs/format.h"

#ifdef __Fuchsia__
#include <fuchsia/hardware/block/c/fidl.h>
#include <lib/zx/vmo.h>

#include <block-client/cpp/block-device.h>
#include <block-client/cpp/client.h>
#include <fs/transaction/device_transaction_handler.h>
#include <storage/buffer/vmo_buffer.h>
#include <storage/buffer/vmoid_registry.h>
#else
#include <fbl/vector.h>
#include <fs/transaction/transaction_handler.h>
#endif

namespace minfs {

#ifdef __Fuchsia__

// A helper function for converting "fd" to "BlockDevice".
zx_status_t FdToBlockDevice(fbl::unique_fd& fd, std::unique_ptr<block_client::BlockDevice>* out);

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
    return block_num * kMinfsBlockSize / info_.block_size;
  }

  block_client::BlockDevice* GetDevice() final { return device_; }

  uint32_t DeviceBlockSize() const;

  // Raw block read functions.
  // These do not track blocks (or attempt to access the block cache)
  // NOTE: Not marked as final, since these are overridden methods on host,
  // but not on __Fuchsia__.
  zx_status_t Readblk(blk_t bno, void* data);
  zx_status_t Writeblk(blk_t bno, const void* data);

  // TODO(rvargas): Move this to BlockDevice.
  // VmoidRegistry interface:
  zx_status_t BlockAttachVmo(const zx::vmo& vmo, storage::Vmoid* out) final;
  zx_status_t BlockDetachVmo(storage::Vmoid vmoid) final;

  ////////////////
  // Other methods.

  // This factory allows building this object from a BlockDevice. Bcache can take ownership of the
  // device (the first Create method), or not (the second Create method).
  static zx_status_t Create(std::unique_ptr<block_client::BlockDevice> device, uint32_t max_blocks,
                            std::unique_ptr<Bcache>* out);

  static zx_status_t Create(block_client::BlockDevice* device, uint32_t max_blocks,
                            std::unique_ptr<Bcache>* out);

  // Returns the maximum number of available blocks,
  // assuming the filesystem is non-resizable.
  uint32_t Maxblk() const { return max_blocks_; }

  block_client::BlockDevice* device() { return device_; }
  const block_client::BlockDevice* device() const { return device_; }

  zx_status_t Sync();

  // Blocks all I/O operations to the underlying device (that go via the RunRequests method). This
  // does *not* block operations that go directly to the device.
  void Pause();

  // Resumes all I/O operations paused by the Pause method.
  void Resume();

 private:
  friend class BlockNode;

  Bcache(block_client::BlockDevice* device, uint32_t max_blocks);

  // Used during initialization of this object.
  zx_status_t VerifyDeviceInfo();

  uint32_t max_blocks_;
  fuchsia_hardware_block_BlockInfo info_ = {};
  std::unique_ptr<block_client::BlockDevice> owned_device_;  // The device, if owned.
  block_client::BlockDevice* device_;  // Pointer to the device, irrespective of ownership.
  // This buffer is used as internal scratch space for the "Readblk/Writeblk" methods.
  storage::VmoBuffer buffer_;
  std::shared_mutex mutex_;
};

#else  // __Fuchsia__

class Bcache : public fs::TransactionHandler {
 public:
  // Not copyable or movable
  Bcache(const Bcache&) = delete;
  Bcache& operator=(const Bcache&) = delete;
  Bcache(Bcache&&) = delete;
  Bcache& operator=(Bcache&&) = delete;

  ~Bcache() {}

  ////////////////
  // fs::TransactionHandler interface.

  uint64_t BlockNumberToDevice(uint64_t block_num) const final { return block_num; }

  zx_status_t RunRequests(const std::vector<storage::BufferedOperation>& operations) final;

  // Raw block read functions.
  // These do not track blocks (or attempt to access the block cache)
  // NOTE: Not marked as final, since these are overridden methods on host,
  // but not on __Fuchsia__.
  zx_status_t Readblk(blk_t bno, void* data);
  zx_status_t Writeblk(blk_t bno, const void* data);

  ////////////////
  // Other methods.

  static zx_status_t Create(fbl::unique_fd fd, uint32_t max_blocks, std::unique_ptr<Bcache>* out);

  // Returns the maximum number of available blocks,
  // assuming the filesystem is non-resizable.
  uint32_t Maxblk() const { return max_blocks_; }

  // Lengths of each extent (in bytes)
  fbl::Array<size_t> extent_lengths_;
  // Tell Bcache to look for Minfs partition starting at |offset| bytes
  zx_status_t SetOffset(off_t offset);
  // Tell the Bcache it is pointing at a sparse file
  // |offset| indicates where the minfs partition begins within the file
  // |extent_lengths| contains the length of each extent (in bytes)
  zx_status_t SetSparse(off_t offset, const fbl::Vector<size_t>& extent_lengths);

  int Sync();

 private:
  friend class BlockNode;

  Bcache(fbl::unique_fd fd, uint32_t max_blocks);

  const fbl::unique_fd fd_;
  uint32_t max_blocks_;
  off_t offset_ = 0;
};

#endif

}  // namespace minfs

#endif  // SRC_STORAGE_MINFS_BCACHE_H_
