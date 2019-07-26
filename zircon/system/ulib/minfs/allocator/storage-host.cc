// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "storage.h"

#include <utility>

namespace minfs {

PersistentStorage::PersistentStorage(SuperblockManager* sb, size_t unit_size, GrowHandler grow_cb,
                                     AllocatorMetadata metadata)
    : sb_(sb), grow_cb_(std::move(grow_cb)), metadata_(std::move(metadata)) {}

zx_status_t PersistentStorage::Extend(WriteTxn* write_transaction, WriteData data,
                                      GrowMapCallback grow_map) {
  return ZX_ERR_NO_SPACE;
}

}  // namespace minfs
