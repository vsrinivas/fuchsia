// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <stdint.h>

#include <fbl/function.h>
#include <fbl/mutex.h>
#include <zircon/types.h>

#include <io-scheduler/stream.h>

namespace ioscheduler {

// Reordering rules for the scheduler.
// Allow reordering of Read class operations with respect to each other.
constexpr uint32_t kOptionReorderReads  = (1u << 0);

// Allow reordering of Write class operations with respect to each other.
constexpr uint32_t kOptionReorderWrites = (1u << 1);

// Allow reordering of Read class operations ahead of Write class operations.
constexpr uint32_t kOptionReorderReadsAheadOfWrites = (1u << 2);

// Allow reordering of Write class operations ahead of Read class operations.
constexpr uint32_t kOptionReorderWritesAheadOfReads = (1u << 3);

// Disallow any reordering.
constexpr uint32_t kOptionStrictlyOrdered = 0;

// Allow all reordering options.
constexpr uint32_t kOptionFullyOutOfOrder = (kOptionReorderReads |
                                             kOptionReorderWrites |
                                             kOptionReorderReadsAheadOfWrites |
                                             kOptionReorderWritesAheadOfReads);

// Maximum priority for a stream.
constexpr uint32_t kMaxPriority = 31;

// Suggested default priority for a stream.
constexpr uint32_t kDefaultPriority = 8;

// Operation classes.
// These are used to determine respective ordering restrictions of the ops in a stream.
enum class OpClass : uint32_t {
    // Operations that can optionally be reordered.

    kOpClassUnknown = 0, // Always reordered.
    kOpClassRead    = 1, // Read ordering.
    kOpClassWrite   = 2, // Write order.
    kOpClassDiscard = 3, // Write order.
    kOpClassRename  = 4, // Read and Write order.
    kOpClassSync    = 5, // Write order.
    kOpClassCommand = 6, // Read and Write order.

    // Operations that cannot be reordered.

    kOpClassOrderedUnknown = 32, // Always ordered.

    // Barrier operations.

    // Prevent reads from being reordered ahead of this barrier op. No read
    // after this barrier can be issued until this operation has completed.
    kOpClassReadBarrier          = 64,

    // Prevent writes from being reordered after this barrier op. This
    // operation completes after all previous writes in the stream have been
    // issued.
    kOpClassWriteBarrier         = 65,

    // Prevent writes from being reordered after this barrier op. This
    // instruction completes after all previous writes in the stream have been
    // completed.
    kOpClassWriteCompleteBarrier = 66,

    // Combined effects of kOpClassReadBarrier and kOpClassWriteBarrier.
    kOpClassFullBarrier          = 67,

    // Combined effects of kOpClassReadBarrier and kOpClassWriteCompleteBarrier.
    kOpClassFullCompleteBarrier  = 68,

};

constexpr uint32_t kOpFlagComplete =    (1u << 0);
constexpr uint32_t kOpFlagGroupLeader = (1u << 8);

// Reserved 64-bit words for internal use.
constexpr size_t kOpReservedQuads = 12;

struct SchedulerOp {
    uint32_t op_class;      // Type of operation.
    uint32_t flags;         // Flags. Should be zero.
    uint32_t stream_id;     // Stream into which this op is queued.
    uint32_t group_id;      // Group of operations.
    uint32_t group_members; // Number of members in the group.
    zx_status_t result;     // Status code of the released operation.
    void* cookie;           // User-defined per-op cookie.
    uint64_t _reserved[kOpReservedQuads]; // Reserved, do not use.
};

// Callback interface from Scheduler to client. Callbacks are made from within
// the Scheduler library to the client implementation. All callbacks are made
// with no locks held and are allowed to block.
class SchedulerClient {
public:
    // CanReorder
    //   Compare if ops can be reordered with respect to each other. This
    // function is called for every pair of ops whose position in
    // the stream is being considered for reorder relative to each other.
    // Returns:
    //   true if it is safe to reorder |second| ahead of |first|.
    //   false otherwise.
    virtual bool CanReorder(SchedulerOp* first, SchedulerOp* second) = 0;

    // Acquire
    //   Read zero or more ops from the client for intake into the
    // Scheduler.
    // Args:
    //   sop_list - an empty array of op pointers to be filled.
    //   list_count - number of entries in sop_list
    //   actual_count - the number of entries filled in sop_list.
    //   wait - block until data is available if true.
    // Returns:
    //   ZX_OK if one or more ops have been added to the list.
    //   ZX_ERR_CANCELED if op source has been closed.
    //   ZX_ERR_SHOULD_WAIT if ops are currently unavailable and |wait| is
    //     false.
    virtual zx_status_t Acquire(SchedulerOp** sop_list, size_t list_count, size_t* actual_count,
                                bool wait) = 0;

    // Issue
    //   Deliver an op to the IO hardware for immediate execution. This
    // function may block until the op is completed. If it does not block,
    // it should return ZX_ERR_ASYNC.
    // Args:
    //   sop - op to be completed.
    // Returns:
    //   ZX_OK if the op has been completed synchronously or it has failed to
    // issue due to bad parameters in the operation. The callee should update
    // the op’s result field to reflect the success or failure status of the
    // op.
    //   ZX_ERR_ASYNC if the op has been issued for asynchronous completion.
    // Notification of completion should be delivered via the Scheduler’s
    // AsyncComplete() API.
    //   Other error status describing the internal failure that has caused
    // the issue to fail.
    virtual zx_status_t Issue(SchedulerOp* sop) = 0;

    // Release
    //   Yield ownership of the operation. The completion status of the op
    // is available in its |result| field. Once released, the Scheduler
    // maintains no references to the op and it can be safely deallocated or
    // reused.
    // Args:
    //   sop - op to be released.
    virtual void Release(SchedulerOp* sop) = 0;

    // CancelAcquire
    //   Cancels any pending blocking calls to Acquire. No further reading of
    // ops should be done. Blocked Acquire callers and any subsequent Acquire
    // calls should return ZX_ERR_CANCELED.
    virtual void CancelAcquire() = 0;

    // Fatal
    //   The Scheduler has encountered a fatal asynchronous error. All pending
    // ops have been aborted. The Scheduler should be shut down and destroyed.
    // The shutdown should be performed from a different context than that of
    // the Fatal() call or else it may deadlock.
    virtual void Fatal() = 0;
};

class Scheduler {
public:
    Scheduler() {}
    ~Scheduler();
    DISALLOW_COPY_ASSIGN_AND_MOVE(Scheduler);

    // Client API - synchronous calls.
    // -------------------------------

    // Initialize a Scheduler object to usable state. Initialize must be called on
    // a newly created Scheduler object or Scheduler that has been shut down
    // before it can be used.
    // The Scheduler holds a pointer to |client| until Shutdown() has returned. It does not
    // manage the lifetime of this pointer and does not free it.
    zx_status_t Init(SchedulerClient* client, uint32_t options);

    // Open a new stream with the requested ID and priority. It is safe to invoke
    // this function from a Scheduler callback context, except from Fatal().
    // |id| may not be that of a currently open stream.
    // |priority| must be in the inclusive range 0 to kMaxPriority.
    // Returns:
    // ZX_OK on success.
    // ZX_ERR_ALREADY_EXISTS if stream with same |id| is already open.
    // ZX_ERR_INVALID_ARGS if |priority| is out of range.
    // Other error status for internal errors.
    zx_status_t StreamOpen(uint32_t id, uint32_t priority);

    // Close an open stream. All ops in the stream will be issued before the stream
    // is closed. New incoming ops to the closed stream will be released with
    // an error.
    zx_status_t StreamClose(uint32_t id);

    // Begin scheduler service. This creates the worker threads that will invoke
    // the callbacks in SchedulerCallbacks.
    zx_status_t Serve();

    // End scheduler service. This function blocks until all outstanding ops in
    // all streams are completed and closes all streams. Shutdown should not be invoked from a
    // callback function. To reuse the scheduler, call Init() again.
    void Shutdown();


    // Client API - asynchronous calls.
    // --------------------------------

    // Asynchronous completion. When an issued operation has completed
    // asynchronously, this function should be called. The status of the operation
    // should be set in |sop|’s result field. This function is non-blocking and
    // safe to call from an interrupt handler context.
    void AsyncComplete(SchedulerOp* sop);

private:
    using StreamIdMap = Stream::WAVLTreeSortById;
    using StreamList = Stream::ListUnsorted;

    SchedulerClient* client_ = nullptr; // Client-supplied callback interface.
    uint32_t options_ = 0;              // Ordering options.

    fbl::Mutex stream_lock_;
    // Number of existing streams.
    uint32_t num_streams_ __TA_GUARDED(stream_lock_) = 0;
    // Number of streams that have ops that need to be issued or completed.
    uint32_t active_streams_ __TA_GUARDED(stream_lock_) = 0;
    // Map of id to stream. Contains all streams.
    StreamIdMap stream_map_ __TA_GUARDED(stream_lock_);
    // List of streams that have ops ready to be scheduled.
    StreamList active_list_ __TA_GUARDED(stream_lock_);
};

} // namespace ioscheduler
