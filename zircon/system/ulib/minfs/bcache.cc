// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <minfs/bcache.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <utility>

#include <fbl/alloc_checker.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>
#include <fs/trace.h>
#include <fuchsia/device/c/fidl.h>
#include <lib/fdio/directory.h>
#include <minfs/format.h>

#include "minfs-private.h"

namespace minfs {

zx_status_t Bcache::Readblk(blk_t bno, void* data) {
  off_t off = static_cast<off_t>(bno) * kMinfsBlockSize;
  assert(off / kMinfsBlockSize == bno);  // Overflow
  if (lseek(fd_.get(), off, SEEK_SET) < 0) {
    FS_TRACE_ERROR("minfs: cannot seek to block %u\n", bno);
    return ZX_ERR_IO;
  }
  if (read(fd_.get(), data, kMinfsBlockSize) != kMinfsBlockSize) {
    FS_TRACE_ERROR("minfs: cannot read block %u\n", bno);
    return ZX_ERR_IO;
  }
  return ZX_OK;
}

zx_status_t Bcache::Writeblk(blk_t bno, const void* data) {
  off_t off = static_cast<off_t>(bno) * kMinfsBlockSize;
  assert(off / kMinfsBlockSize == bno);  // Overflow
  if (lseek(fd_.get(), off, SEEK_SET) < 0) {
    FS_TRACE_ERROR("minfs: cannot seek to block %u. %d\n", bno, errno);
    return ZX_ERR_IO;
  }
  ssize_t ret = write(fd_.get(), data, kMinfsBlockSize);
  if (ret != kMinfsBlockSize) {
    FS_TRACE_ERROR("minfs: cannot write block %u (%zd)\n", bno, ret);
    return ZX_ERR_IO;
  }
  return ZX_OK;
}

int Bcache::Sync() {
  fs::WriteTxn sync_txn(this);
  sync_txn.EnqueueFlush();
  return sync_txn.Transact();
}

// Static.
zx_status_t Bcache::Create(fbl::unique_fd fd, uint32_t max_blocks, std::unique_ptr<Bcache>* out) {
  zx::channel channel, server;
  zx_status_t status = zx::channel::create(0, &channel, &server);
  if (status != ZX_OK) {
    return status;
  }
  fzl::UnownedFdioCaller caller(fd.get());
  uint32_t flags = ZX_FS_FLAG_CLONE_SAME_RIGHTS;
  status = fuchsia_io_NodeClone(caller.borrow_channel(), flags, server.release());
  if (status != ZX_OK) {
    return status;
  }

  std::unique_ptr<block_client::RemoteBlockDevice> device;
  status = block_client::RemoteBlockDevice::Create(std::move(channel), &device);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("minfs: cannot create block device: %d\n", status);
    return status;
  }

  fbl::AllocChecker ac;
  fbl::unique_ptr<Bcache> bc(new (&ac) Bcache(std::move(fd), std::move(device), max_blocks));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  status = bc->device_->BlockGetInfo(&bc->info_);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("minfs: cannot get block device information: %d\n", status);
    return status;
  }

  if (kMinfsBlockSize % bc->info_.block_size != 0) {
    FS_TRACE_ERROR("minfs: minfs Block size not multiple of underlying block size: %d\n",
                   bc->info_.block_size);
    return ZX_ERR_BAD_STATE;
  }

  *out = std::move(bc);
  return ZX_OK;
}

groupid_t Bcache::BlockGroupID() {
  thread_local groupid_t group = next_group_.fetch_add(1);
  ZX_ASSERT_MSG(group < MAX_TXN_GROUP_COUNT, "Too many threads accessing block device");
  return group;
}

uint32_t Bcache::DeviceBlockSize() const { return info_.block_size; }

Bcache::Bcache(fbl::unique_fd fd, std::unique_ptr<block_client::BlockDevice> device,
               uint32_t max_blocks)
    : fd_(std::move(fd)), max_blocks_(max_blocks), device_(std::move(device)) {}

}  // namespace minfs
