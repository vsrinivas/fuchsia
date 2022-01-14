// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_STORAGE_BLOCK_CLIENT_CPP_READER_H_
#define SRC_LIB_STORAGE_BLOCK_CLIENT_CPP_READER_H_

#include <lib/fzl/owned-vmo-mapper.h>

#include <storage/buffer/owned_vmoid.h>

#include "src/lib/storage/block_client/cpp/block_device.h"

namespace block_client {

// Reader provides a simple wrapper around a block device that permits reading from a device without
// having to worry about VMOs. It should not be used if performance is a concern and it is *not*
// thread-safe.
class Reader {
 public:
  Reader(BlockDevice& device) : device_(device) {}

  // Reads `count` bytes from the device at offset `offset`.  Both `count` and `offset` must be
  // aligned to the device block size.
  zx_status_t Read(uint64_t offset, size_t count, void* buf);

 private:
  BlockDevice& device_;
  uint64_t block_size_ = 0;
  fzl::OwnedVmoMapper buffer_;
  storage::OwnedVmoid vmoid_;
};

}  // namespace block_client

#endif  // SRC_LIB_STORAGE_BLOCK_CLIENT_CPP_READER_H_
