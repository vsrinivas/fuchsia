// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <utility>

#include <fbl/alloc_checker.h>
#include <fbl/ref_ptr.h>
#include <fs/trace.h>
#include <storage/buffer/block_buffer.h>
#include <storage/operation/operation.h>

#include "src/storage/minfs/bcache.h"
#include "src/storage/minfs/format.h"
#include "src/storage/minfs/minfs_private.h"

namespace minfs {

zx_status_t Bcache::RunRequests(const std::vector<storage::BufferedOperation>& operations) {
  for (const storage::BufferedOperation& operation : operations) {
    if (operation.op.type != storage::OperationType::kWrite &&
        operation.op.type != storage::OperationType::kRead) {
      return ZX_ERR_NOT_SUPPORTED;
    }

    // TODO(fxbug.dev/47947): Clean up this hack.
    void* data = static_cast<uint8_t*>(operation.data) + operation.op.vmo_offset * kMinfsBlockSize;
    ssize_t result;
    if (operation.op.type == storage::OperationType::kRead) {
      result = pread(fd_.get(), data, operation.op.length * kMinfsBlockSize,
                     operation.op.dev_offset * kMinfsBlockSize);
    } else {
      result = pwrite(fd_.get(), data, operation.op.length * kMinfsBlockSize,
                      operation.op.dev_offset * kMinfsBlockSize);
    }

    if (result != static_cast<ssize_t>(operation.op.length * kMinfsBlockSize)) {
      // Linux and Mac don't agree on the number of "longs" on uint64_t.
      FS_TRACE_ERROR("minfs: RunOperation %s failure at block 0x%lx (%zd)\n",
                     operation.op.type == storage::OperationType::kRead ? "read" : "write",
                     static_cast<unsigned long>(operation.op.dev_offset), result);
      return ZX_ERR_IO;
    }
  }
  return ZX_OK;
}

zx_status_t Bcache::Readblk(blk_t bno, void* data) {
  off_t off = static_cast<off_t>(bno) * kMinfsBlockSize;
  assert(off / kMinfsBlockSize == bno);  // Overflow
  off += offset_;
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
  off += offset_;
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

zx_status_t Bcache::Sync() {
  // No-op.
  return ZX_OK;
}

// Static.
zx_status_t Bcache::Create(fbl::unique_fd fd, uint32_t max_blocks, std::unique_ptr<Bcache>* out) {
  out->reset(new Bcache(std::move(fd), max_blocks));
  return ZX_OK;
}

Bcache::Bcache(fbl::unique_fd fd, uint32_t max_blocks)
    : fd_(std::move(fd)), max_blocks_(max_blocks) {}

zx_status_t Bcache::SetOffset(off_t offset) {
  if (offset_ || extent_lengths_.size() > 0) {
    return ZX_ERR_ALREADY_BOUND;
  }
  offset_ = offset;
  return ZX_OK;
}

zx_status_t Bcache::SetSparse(off_t offset, const fbl::Vector<size_t>& extent_lengths) {
  if (offset_ || extent_lengths_.size() > 0) {
    return ZX_ERR_ALREADY_BOUND;
  }

  ZX_ASSERT(extent_lengths.size() == kExtentCount);

  fbl::AllocChecker ac;
  extent_lengths_.reset(new (&ac) size_t[kExtentCount], kExtentCount);

  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  for (size_t i = 0; i < extent_lengths.size(); i++) {
    extent_lengths_[i] = extent_lengths[i];
  }

  offset_ = offset;
  return ZX_OK;
}

}  // namespace minfs
