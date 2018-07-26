// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/driver.h>
#include <ddk/metadata/bad-block.h>
#include <ddk/protocol/nand.h>
#include <ddktl/protocol/nand.h>

#include <fbl/array.h>
#include <fbl/mutex.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <lib/zx/vmo.h>
#include <zircon/thread_annotations.h>
#include <zircon/types.h>

namespace nand {

// Interface for interacting with device bad blocks.
class BadBlock : public fbl::RefCounted<BadBlock> {
public:
    struct Config {
        // Bad block configuration for device.
        bad_block_config_t bad_block_config;
        // Parent device NAND protocol.
        nand_protocol_t nand_proto;
    };

    static zx_status_t Create(Config config, fbl::RefPtr<BadBlock>* out);

    virtual ~BadBlock() = default;

    // Returns a list of bad blocks between [first_block, last_block).
    virtual zx_status_t GetBadBlockList(uint32_t first_block, uint32_t last_block,
                                        fbl::Array<uint32_t>* bad_blocks) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    // Marks a block bad and updates underlying storage.
    virtual zx_status_t MarkBlockBad(uint32_t block) { return ZX_ERR_NOT_SUPPORTED; }

protected:
    BadBlock(zx::vmo data_vmo, zx::vmo oob_vmo, fbl::Array<uint8_t> nand_op)
        : data_vmo_(fbl::move(data_vmo)),
          oob_vmo_(fbl::move(oob_vmo)),
          nand_op_(fbl::move(nand_op)) {}

    // Ensures serialized access.
    fbl::Mutex lock_;
    // VMO with data buffer. Size is dependent on bad block implementation.
    zx::vmo data_vmo_ TA_GUARDED(lock_);
    // VMO with oob buffer. Size is dependent on bad block implementation.
    zx::vmo oob_vmo_ TA_GUARDED(lock_);
    // Operation buffer of size parent_op_size.
    fbl::Array<uint8_t> nand_op_ TA_GUARDED(lock_);
};

} // namespace nand
