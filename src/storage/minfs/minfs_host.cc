// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/minfs/minfs_private.h"

#ifdef __Fuchsia__
static_assert(false, "This file is not meant to be used on target");
#endif  // __Fuchsia__

namespace minfs {

[[nodiscard]] bool Minfs::IsJournalErrored() { return false; }

std::vector<fbl::RefPtr<VnodeMinfs>> Minfs::GetDirtyVnodes() {
  std::vector<fbl::RefPtr<VnodeMinfs>> vnodes;
  return vnodes;
}

zx::status<> Minfs::ContinueTransaction(size_t reserve_blocks,
                                        std::unique_ptr<CachedBlockTransaction> cached_transaction,
                                        std::unique_ptr<Transaction>* out) {
  // Reserve blocks from allocators before returning WritebackWork to client.
  *out = Transaction::FromCachedBlockTransaction(this, std::move(cached_transaction));

  if (auto status = (*out)->ExtendBlockReservation(reserve_blocks); status.is_error()) {
    return status.take_error();
  }

  return zx::ok();
}

}  // namespace minfs
