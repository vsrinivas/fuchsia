// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <blobfs/unbuffered-operations-builder.h>

namespace blobfs {
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

        uint64_t old_start = operation.op.vmo_offset;
        uint64_t old_end = operation.op.vmo_offset + operation.op.length;

        uint64_t new_start = vmo_offset;
        uint64_t new_end = vmo_offset + length;

        if ((old_start <= new_start) && (new_end <= old_end)) {
            // The old operation is larger (on both sides) than the new operation.
            //
            // It's already as large as it needs to be; exit.
            return;
        } else if ((new_start <= old_start) && (old_end <= new_end)) {
            // The new operation is larger (on both sides) than the old operation.
            //
            // Make the old operation as large as the new operation.
            operation.op.vmo_offset = vmo_offset;
            operation.op.dev_offset = dev_offset;
            block_count_ += (length - operation.op.length);
            operation.op.length = length;
            return;
        } else if ((new_start <= old_end) && (old_start <= new_start)) {
            // The new op either partially or totally follows the old operation.
            //
            // Post-Extend the old operation.
            size_t extension = new_end - old_end;
            operation.op.length += extension;
            block_count_ += extension;
            return;
        } else if ((old_start <= new_end) && (new_start <= old_start)) {
            // The new op either partially or totally precedes the old operation.
            //
            // Pre-Extend the old operation.
            size_t extension = old_start - new_start;
            operation.op.vmo_offset = vmo_offset;
            operation.op.dev_offset = dev_offset;
            operation.op.length += extension;
            block_count_ += extension;
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

uint64_t BlockCount(const fbl::Vector<UnbufferedOperation>& operations) {
    uint64_t total_length = 0;
    for (const auto& operation: operations) {
        total_length += operation.op.length;
    }
    return total_length;
}

} // namespace blobfs
