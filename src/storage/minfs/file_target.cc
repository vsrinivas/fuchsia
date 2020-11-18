// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fs/trace.h"
#include "src/storage/minfs/file.h"
#include "src/storage/minfs/minfs_private.h"
#include "zircon/assert.h"

// This file contains Fuchsia specific minfs::File code.

namespace minfs {

File::~File() {
  ZX_DEBUG_ASSERT_MSG(allocation_state_.GetNodeSize() == GetInode()->size,
                      "File being destroyed with pending updates to the inode size");
}

bool File::DirtyCacheEnabled() const {
  // We don't yet enable dirty cache on target.
  ZX_ASSERT(!Minfs::DirtyCacheEnabled());
  return Minfs::DirtyCacheEnabled();
}

zx::status<> File::ForceFlushTransaction(std::unique_ptr<Transaction> transaction) {
  // Ensure this Vnode remains alive while it has an operation in-flight.
  transaction->PinVnode(fbl::RefPtr(this));
  AllocateAndCommitData(std::move(transaction));
  return zx::ok();
}

zx::status<> File::FlushTransaction(std::unique_ptr<Transaction> transaction, bool force_flush) {
  return ForceFlushTransaction(std::move(transaction));
}

}  // namespace minfs
