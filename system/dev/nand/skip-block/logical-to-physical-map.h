// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/array.h>
#include <fbl/macros.h>
#include <fbl/type_support.h>
#include <zircon/types.h>

namespace nand {

// Logical block to physical block mapping. Provides bad block skip
// functionality. If more than one copy is required, the logical space for each
// copy begins at the physical block |block_count_| / |copy|, and bad blocks are
// skipped from there.
class LogicalToPhysicalMap {
public:
    LogicalToPhysicalMap()
        : copies_(0), block_count_(0) {}

    // Constructor.
    LogicalToPhysicalMap(uint32_t copies, uint32_t block_count, fbl::Array<uint32_t> bad_blocks);

    // Move constructor.
    LogicalToPhysicalMap(LogicalToPhysicalMap&& other)
        : copies_(other.copies_), block_count_(other.block_count_),
          bad_blocks_(fbl::move(other.bad_blocks_)) {}

    // Move assignment operator.
    LogicalToPhysicalMap& operator=(LogicalToPhysicalMap&& other) {
        if (this != &other) {
            copies_ = other.copies_;
            block_count_ = other.block_count_;
            bad_blocks_ = fbl::move(other.bad_blocks_);
        }
        return *this;
    }

    zx_status_t GetPhysical(uint32_t copy, uint32_t block, uint32_t* physical_block) const;

    uint32_t LogicalBlockCount(uint32_t copy) const;

private:
    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(LogicalToPhysicalMap);

    uint32_t copies_;
    uint32_t block_count_;
    fbl::Array<uint32_t> bad_blocks_;
};

} // namespace nand
