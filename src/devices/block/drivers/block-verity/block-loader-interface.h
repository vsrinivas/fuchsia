// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BLOCK_DRIVERS_BLOCK_VERITY_BLOCK_LOADER_INTERFACE_H_
#define SRC_DEVICES_BLOCK_DRIVERS_BLOCK_VERITY_BLOCK_LOADER_INTERFACE_H_

#include <lib/zx/vmo.h>

namespace block_verity {

typedef void (*BlockLoaderCallback)(void* cookie, zx_status_t status);

// Interface for requesting reads of blocks from some I/O provider implementation.
class BlockLoaderInterface {
 public:
  // Requests blocks `start_block` through `start_block + block_count - 1`
  // blocks, writes their contents to `vmo`, and then calls `callback` with
  // `cookie` as the first argument and a status representing the success or
  // failure of the load.
  virtual void RequestBlocks(uint64_t start_block, uint64_t block_count, zx::vmo& vmo, void* cookie,
                             BlockLoaderCallback callback) = 0;
};

}  // namespace block_verity

#endif  // SRC_DEVICES_BLOCK_DRIVERS_BLOCK_VERITY_BLOCK_LOADER_INTERFACE_H_
