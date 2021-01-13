// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BLOCK_DRIVERS_BLOCK_VERITY_CONFIG_H_
#define SRC_DEVICES_BLOCK_DRIVERS_BLOCK_VERITY_CONFIG_H_

#include <fuchsia/hardware/block/cpp/banjo.h>
#include <fuchsia/hardware/block/verified/llcpp/fidl.h>
#include <zircon/errors.h>

namespace block_verity {

// Checks that `config` specifies both a root hash and a block size, and that
// the block size matches the one observed in `blk`.  Returns ZX_OK on success,
// ZX_ERR_INVALID_ARGS otherwise.
zx_status_t CheckConfig(const llcpp::fuchsia::hardware::block::verified::Config& config,
                        const block_info_t& blk);

}  // namespace block_verity

#endif  // SRC_DEVICES_BLOCK_DRIVERS_BLOCK_VERITY_CONFIG_H_
