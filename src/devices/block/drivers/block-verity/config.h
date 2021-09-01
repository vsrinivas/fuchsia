// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BLOCK_DRIVERS_BLOCK_VERITY_CONFIG_H_
#define SRC_DEVICES_BLOCK_DRIVERS_BLOCK_VERITY_CONFIG_H_

#include <fidl/fuchsia.hardware.block.verified/cpp/wire.h>
#include <fuchsia/hardware/block/cpp/banjo.h>
#include <zircon/errors.h>

namespace block_verity {

// Checks that `config` specifies both a root hash and a block size, and that
// the block size matches the one observed in `blk`.  Returns ZX_OK on success,
// ZX_ERR_INVALID_ARGS otherwise.
zx_status_t CheckConfig(const fuchsia_hardware_block_verified::wire::Config& config,
                        const block_info_t& blk);

}  // namespace block_verity

#endif  // SRC_DEVICES_BLOCK_DRIVERS_BLOCK_VERITY_CONFIG_H_
