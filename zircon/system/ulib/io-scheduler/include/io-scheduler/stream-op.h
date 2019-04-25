// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

#include <zircon/types.h>

namespace ioscheduler {

// Operation type.
// These are used to determine respective ordering restrictions of the ops in a stream.
enum class OpType : uint32_t {
    // Operations that can optionally be reordered.

    kOpTypeUnknown = 0, // Always reordered.
    kOpTypeRead    = 1, // Read ordering.
    kOpTypeWrite   = 2, // Write order.
    kOpTypeDiscard = 3, // Write order.
    kOpTypeRename  = 4, // Read and Write order.
    kOpTypeSync    = 5, // Write order.
    kOpTypeCommand = 6, // Read and Write order.

    // Operations that cannot be reordered.

    kOpTypeOrderedUnknown = 32, // Always ordered.

    // Barrier operations.

    // Prevent reads from being reordered ahead of this barrier op. No read
    // after this barrier can be issued until this operation has completed.
    kOpTypeReadBarrier          = 64,

    // Prevent writes from being reordered after this barrier op. This
    // operation completes after all previous writes in the stream have been
    // issued.
    kOpTypeWriteBarrier         = 65,

    // Prevent writes from being reordered after this barrier op. This
    // instruction completes after all previous writes in the stream have been
    // completed.
    kOpTypeWriteCompleteBarrier = 66,

    // Combined effects of kOpTypeReadBarrier and kOpTypeWriteBarrier.
    kOpTypeFullBarrier          = 67,

    // Combined effects of kOpTypeReadBarrier and kOpTypeWriteCompleteBarrier.
    kOpTypeFullCompleteBarrier  = 68,
};

constexpr uint32_t kOpFlagComplete =    (1u << 0);
constexpr uint32_t kOpFlagGroupLeader = (1u << 8);

constexpr uint32_t kOpGroupNone = 0;

struct StreamOp {
    OpType type;            // Type of operation.
    uint32_t flags;         // Flags. Should be zero.
    uint32_t stream_id;     // Stream into which this op is queued.
    uint32_t group_id;      // Group of operations.
    uint32_t group_members; // Number of members in the group.
    zx_status_t result;     // Status code of the released operation.
    void* cookie;           // User-defined per-op cookie.
};

// UniqueOp is a wrapper around StreamOp designed to clarify the ownership of an op pointer.
// It supports move-only semantics, and must be either move()'d or release()'d before destruction.
// Since StreamOp is allocated by the client, it cannot be deleted by this wrapper. Notably,
// UniqueOp's destructor DOES NOT delete and will assert if its container is non-null.
class UniqueOp {
public:
    // Constructors
    constexpr UniqueOp() : op_(nullptr) {}
    constexpr UniqueOp(decltype(nullptr)) : UniqueOp() {}
    explicit UniqueOp(StreamOp* op) : op_(op) {}

    // Copy construction.
    UniqueOp(const UniqueOp& r) = delete;
    // Assignment
    UniqueOp& operator=(const UniqueOp& r) = delete;


    // Move construction.
    UniqueOp(UniqueOp&& r) : op_(r.op_) { r.op_ = nullptr; }

    ~UniqueOp() {
        ZX_DEBUG_ASSERT(op_ == nullptr);
    }

    // Move assignment.
    UniqueOp& operator=(UniqueOp&& r) {
        UniqueOp(std::move(r));
        return *this;
    }

    StreamOp* release() {
        StreamOp* old = op_;
        op_ = nullptr;
        return old;
    }

    StreamOp* get() const { return op_; }
    StreamOp& operator*() const { return *op_; }
    StreamOp* operator->() const { return op_; }
    explicit operator bool() const { return !!op_; }
    bool operator==(decltype(nullptr)) const  { return (op_ == nullptr); }
    bool operator!=(decltype(nullptr)) const  { return (op_ != nullptr); }
    bool operator==(const UniqueOp& other) const { return (op_ == other.op_); }
    bool operator!=(const UniqueOp& other) const { return (op_ != other.op_); }

private:
    StreamOp* op_ = nullptr;
};

} // namespace ioscheduler
