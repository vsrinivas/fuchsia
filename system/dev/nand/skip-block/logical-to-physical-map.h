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
// functionality.
//
// NOT THREADSAFE.
class LogicalToPhysicalMap {
public:
    LogicalToPhysicalMap()
        : block_count_(0) {}

    // Constructor. |bad_blocks| is expected to be a sorted list of physical
    // block numbers.
    LogicalToPhysicalMap(uint32_t block_count, fbl::Array<uint32_t> bad_blocks);

    // Move constructor.
    LogicalToPhysicalMap(LogicalToPhysicalMap&& other)
        : block_count_(other.block_count_), bad_blocks_(fbl::move(other.bad_blocks_)) {}

    // Move assignment operator.
    LogicalToPhysicalMap& operator=(LogicalToPhysicalMap&& other) {
        if (this != &other) {
            block_count_ = other.block_count_;
            bad_blocks_ = fbl::move(other.bad_blocks_);
        }
        return *this;
    }

    zx_status_t GetPhysical(uint32_t block, uint32_t* physical_block) const;

    uint32_t LogicalBlockCount() const {
        return block_count_ - static_cast<uint32_t>(bad_blocks_.size());
    }

private:
    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(LogicalToPhysicalMap);

    uint32_t block_count_;
    fbl::Array<uint32_t> bad_blocks_;
};

} // namespace nand
