// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <lib/syslog/cpp/macros.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <iomanip>
#include <utility>

#include <fbl/alloc_checker.h>
#include <fbl/ref_ptr.h>
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
      FX_LOGS(ERROR) << "RunOperation "
                     << (operation.op.type == storage::OperationType::kRead ? "read" : "write")
                     << " failure at block 0x" << std::hex << operation.op.dev_offset
                     << " result=" << std::dec << result;
      return ZX_ERR_IO;
    }
  }
  return ZX_OK;
}

zx::status<> Bcache::Readblk(blk_t bno, void* data) {
  off_t off = static_cast<off_t>(bno) * kMinfsBlockSize;
  assert(off / kMinfsBlockSize == bno);  // Overflow
  off += offset_;
  if (lseek(fd_.get(), off, SEEK_SET) < 0) {
    FX_LOGS(ERROR) << "cannot seek to block " << bno;
    return zx::error(ZX_ERR_IO);
  }
  if (read(fd_.get(), data, kMinfsBlockSize) != kMinfsBlockSize) {
    FX_LOGS(ERROR) << "cannot read block " << bno;
    return zx::error(ZX_ERR_IO);
  }
  return zx::ok();
}

zx::status<> Bcache::Writeblk(blk_t bno, const void* data) {
  off_t off = static_cast<off_t>(bno) * kMinfsBlockSize;
  assert(off / kMinfsBlockSize == bno);  // Overflow
  off += offset_;
  if (lseek(fd_.get(), off, SEEK_SET) < 0) {
    FX_LOGS(ERROR) << "cannot seek to block " << bno << ". " << errno;
    return zx::error(ZX_ERR_IO);
  }
  ssize_t ret = write(fd_.get(), data, kMinfsBlockSize);
  if (ret != kMinfsBlockSize) {
    FX_LOGS(ERROR) << "cannot write block " << bno << " (" << ret << ")";
    return zx::error(ZX_ERR_IO);
  }
  return zx::ok();
}

zx::status<> Bcache::Sync() {
  // No-op.
  return zx::ok();
}

// static
zx::status<std::unique_ptr<Bcache>> Bcache::Create(fbl::unique_fd fd, uint32_t max_blocks) {
  return zx::ok(std::unique_ptr<Bcache>(new Bcache(std::move(fd), max_blocks)));
}

Bcache::Bcache(fbl::unique_fd fd, uint32_t max_blocks)
    : fd_(std::move(fd)), max_blocks_(max_blocks) {}

zx::status<> Bcache::SetOffset(off_t offset) {
  if (offset_ || !extent_lengths_.empty()) {
    return zx::error(ZX_ERR_ALREADY_BOUND);
  }
  offset_ = offset;
  return zx::ok();
}

zx::status<> Bcache::SetSparse(off_t offset, const fbl::Vector<size_t>& extent_lengths) {
  if (offset_ || !extent_lengths_.empty()) {
    return zx::error(ZX_ERR_ALREADY_BOUND);
  }

  ZX_ASSERT(extent_lengths.size() == kExtentCount);

  fbl::AllocChecker ac;
  extent_lengths_.reset(new (&ac) size_t[kExtentCount], kExtentCount);

  if (!ac.check()) {
    return zx::error(ZX_ERR_NO_MEMORY);
  }

  for (size_t i = 0; i < extent_lengths.size(); i++) {
    extent_lengths_[i] = extent_lengths[i];
  }

  offset_ = offset;
  return zx::ok();
}

}  // namespace minfs
