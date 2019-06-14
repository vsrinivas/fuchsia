// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <cstdint>

#include <blobfs/unbuffered-operations-builder.h>
#include <range/range.h>

namespace blobfs {
using range::Range;
namespace {

// Skew between the vmo offset and the device offset implies that
// operations should not be combined.
bool EqualVmoDeviceOffsetSkew(const Operation& a, const Operation& b) {
    return (a.vmo_offset - b.vmo_offset) == (a.dev_offset - b.dev_offset);
}

} // namespace

UnbufferedOperationsBuilder::~UnbufferedOperationsBuilder() {}

void UnbufferedOperationsBuilder::Add(const UnbufferedOperation& new_operation) {
    ZX_DEBUG_ASSERT(new_operation.vmo->is_valid());

    zx::unowned_vmo vmo = zx::unowned_vmo(new_operation.vmo->get());
    uint64_t vmo_offset = new_operation.op.vmo_offset;
    uint64_t dev_offset = new_operation.op.dev_offset;
    uint64_t length = new_operation.op.length;

    if (length == 0) {
        return;
    }

    for (auto& operation : operations_) {
        if ((operation.vmo->get() != vmo->get()) ||
            (operation.op.type != new_operation.op.type) ||
            !EqualVmoDeviceOffsetSkew(operation.op, new_operation.op)) {
            continue;
        }

        // TODO(ZX-4238): Merge/coalesce is more involved than this. One enqueue can encompass from
        //                one to all existing requests - leading to popping out operations.
        auto old_range =
            Range<uint64_t>(operation.op.vmo_offset, operation.op.vmo_offset + operation.op.length);

        auto new_range = Range<uint64_t>(vmo_offset, vmo_offset + length);
        if (Mergable(old_range, new_range)) {
            auto merged_op = Merge(old_range, new_range).value();
            operation.op.vmo_offset = merged_op.start();
            operation.op.length = merged_op.length();
            operation.op.dev_offset = std::min(dev_offset, operation.op.dev_offset);
            block_count_ += (merged_op.length() - old_range.length());
            return;
        }
    }

    UnbufferedOperation operation;
    operation.vmo = zx::unowned_vmo(vmo->get());
    operation.op.type = new_operation.op.type;
    operation.op.vmo_offset = vmo_offset;
    operation.op.dev_offset = dev_offset;
    operation.op.length = length;
    operations_.push_back(operation);
    block_count_ += operation.op.length;
}

fbl::Vector<UnbufferedOperation> UnbufferedOperationsBuilder::TakeOperations() {
    block_count_ = 0;
    return std::move(operations_);
}

} // namespace blobfs
