// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include <minfs/format.h>

#include "storage.h"

namespace minfs {

PersistentStorage::PersistentStorage(SuperblockManager* sb, size_t unit_size, GrowHandler grow_cb,
                                     AllocatorMetadata metadata, uint32_t block_size)
    : sb_(sb),
      grow_cb_(std::move(grow_cb)),
      metadata_(std::move(metadata)),
      block_size_(block_size) {}

zx_status_t PersistentStorage::Extend(PendingWork* write_transaction, WriteData data,
                                      GrowMapCallback grow_map) {
  return ZX_ERR_NO_SPACE;
}

}  // namespace minfs
