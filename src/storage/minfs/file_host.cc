// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/minfs/file.h"
#include "src/storage/minfs/minfs_private.h"
#include "zircon/assert.h"

// This file contains host specific minfs::File code.

namespace minfs {

File::~File() = default;

// We don't enable dirty cache on host.
bool File::DirtyCacheEnabled() const {
  ZX_ASSERT(!Minfs::DirtyCacheEnabled());
  return false;
}

zx::status<> File::ForceFlushTransaction(std::unique_ptr<Transaction> transaction) {
  // Ensure this Vnode remains alive while it has an operation in-flight.
  transaction->PinVnode(fbl::RefPtr(this));
  InodeSync(transaction.get(), kMxFsSyncMtime);  // Successful write/truncate updates mtime
  Vfs()->CommitTransaction(std::move(transaction));
  return zx::ok();
}

zx::status<> File::FlushTransaction(std::unique_ptr<Transaction> transaction, bool force) {
  return ForceFlushTransaction(std::move(transaction));
}

}  // namespace minfs
