// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file describes the in-memory structures which construct
// a MinFS filesystem.

#ifndef MINFS_BCACHE_H_
#define MINFS_BCACHE_H_

#include <errno.h>
#include <inttypes.h>

#include <atomic>

#include <fbl/algorithm.h>
#include <fbl/array.h>
#include <fbl/macros.h>
#include <fbl/unique_fd.h>
#include <fs/trace.h>
#include <fs/transaction/block_transaction.h>
#include <fs/vfs.h>
#include <fs/vnode.h>
#include <minfs/format.h>

#ifdef __Fuchsia__
#include <fuchsia/hardware/block/c/fidl.h>
#include <fuchsia/hardware/block/volume/c/fidl.h>
#include <lib/fzl/fdio.h>
#include <lib/zx/vmo.h>

#include <block-client/cpp/block-device.h>
#include <block-client/cpp/block-group-registry.h>
#include <block-client/cpp/client.h>
#include <fvm/client.h>
#include <storage/buffer/vmo_buffer.h>
#include <storage/buffer/vmoid_registry.h>
#else
#include <fbl/vector.h>
#endif

namespace minfs {

#ifdef __Fuchsia__

// A helper function for converting "fd" to "BlockDevice".
zx_status_t FdToBlockDevice(fbl::unique_fd& fd, std::unique_ptr<block_client::BlockDevice>* out);

class Bcache : public fs::TransactionHandler, public storage::VmoidRegistry {
 public:
  DISALLOW_COPY_ASSIGN_AND_MOVE(Bcache);
  friend class BlockNode;

  ~Bcache() = default;

  // Destroys a "bcache" object, but take back ownership of the underlying block device.
  static std::unique_ptr<block_client::BlockDevice> Destroy(std::unique_ptr<Bcache> bcache);

  ////////////////
  // fs::TransactionHandler interface.

  uint32_t FsBlockSize() const final { return kMinfsBlockSize; }

  uint64_t BlockNumberToDevice(uint64_t block_num) const final {
    return block_num * kMinfsBlockSize / DeviceBlockSize();
  }

  zx_status_t RunOperation(const storage::Operation& operation, storage::BlockBuffer* buffer) final;

  groupid_t BlockGroupID() final;

  uint32_t DeviceBlockSize() const final;

  block_client::BlockDevice* GetDevice() final { return device_.get(); }

  zx_status_t Transaction(block_fifo_request_t* requests, size_t count) final {
    return device_->FifoTransaction(requests, count);
  }

  // Raw block read functions.
  // These do not track blocks (or attempt to access the block cache)
  // NOTE: Not marked as final, since these are overridden methods on host,
  // but not on __Fuchsia__.
  zx_status_t Readblk(blk_t bno, void* data);
  zx_status_t Writeblk(blk_t bno, const void* data);

  // TODO(rvargas): Move this to BlockDevice.
  // VmoidRegistry interface:
  zx_status_t AttachVmo(const zx::vmo& vmo, vmoid_t* out) final;
  zx_status_t DetachVmo(vmoid_t vmoid) final;

  ////////////////
  // Other methods.

  // This factory allows building this object from a BlockDevice.
  static zx_status_t Create(std::unique_ptr<block_client::BlockDevice> device, uint32_t max_blocks,
                            std::unique_ptr<Bcache>* out);

  // Returns the maximum number of available blocks,
  // assuming the filesystem is non-resizable.
  uint32_t Maxblk() const { return max_blocks_; }

  block_client::BlockDevice* device() { return device_.get(); }
  const block_client::BlockDevice* device() const { return device_.get(); }

  int Sync();

 private:
  Bcache(std::unique_ptr<block_client::BlockDevice> device, uint32_t max_blocks);

  // Used during initialization of this object.
  zx_status_t VerifyDeviceInfo();

  uint32_t max_blocks_;
  fuchsia_hardware_block_BlockInfo info_ = {};
  block_client::BlockGroupRegistry group_registry_;
  std::unique_ptr<block_client::BlockDevice> device_;
  // This buffer is used as internal scratch space for the "Readblk/Writeblk" methods.
  storage::VmoBuffer buffer_;
};

#else  // __Fuchsia__

class Bcache : public fs::TransactionHandler {
 public:
  DISALLOW_COPY_ASSIGN_AND_MOVE(Bcache);
  friend class BlockNode;

  ~Bcache() {}

  ////////////////
  // fs::TransactionHandler interface.

  uint32_t FsBlockSize() const final { return kMinfsBlockSize; }

  uint64_t BlockNumberToDevice(uint64_t block_num) const final { return block_num; }

  zx_status_t RunOperation(const storage::Operation& operation, storage::BlockBuffer* buffer) final;

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
  Bcache(fbl::unique_fd fd, uint32_t max_blocks);

  const fbl::unique_fd fd_;
  uint32_t max_blocks_;
  off_t offset_ = 0;
};

#endif

}  // namespace minfs

#endif  // MINFS_BCACHE_H_
