// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/block/drivers/block-verity/config.h"

#include <ddk/debug.h>

#include "src/devices/block/drivers/block-verity/constants.h"

namespace block_verity {

zx_status_t CheckConfig(const llcpp::fuchsia::hardware::block::verified::Config& config,
                        const block_info_t& blk) {
  // Check that the config specifies a supported hash function
  if (!config.has_hash_function()) {
    zxlogf(WARNING, "Config did not specify a hash function");
    return ZX_ERR_INVALID_ARGS;
  }
  switch (config.hash_function()) {
    case llcpp::fuchsia::hardware::block::verified::HashFunction::SHA256:
      break;
    default:
      zxlogf(WARNING, "Unknown hash function enum value %hhu", config.hash_function());
      return ZX_ERR_INVALID_ARGS;
  }

  // Check that the config specifies a supported block size, and that the block
  // size matches that of the underlying block device
  if (!config.has_block_size()) {
    zxlogf(WARNING, "Config did not specify a block size");
    return ZX_ERR_INVALID_ARGS;
  }
  switch (config.block_size()) {
    case llcpp::fuchsia::hardware::block::verified::BlockSize::SIZE_4096:
      // Verify that the block size from the device matches the value requested.
      if (kBlockSize % blk.block_size != 0) {
        zxlogf(WARNING,
               "Config specified block size 4096 but underlying block size %d "
               "does not evenly divide 4096",
               blk.block_size);
        return ZX_ERR_INVALID_ARGS;
      }
      break;
    default:
      zxlogf(WARNING, "Unknown block size enum value %hhu", config.block_size());
      return ZX_ERR_INVALID_ARGS;
  }

  return ZX_OK;
}

}  // namespace block_verity
