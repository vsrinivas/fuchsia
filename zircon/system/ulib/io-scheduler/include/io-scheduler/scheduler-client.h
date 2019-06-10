// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/types.h>

#include <io-scheduler/stream-op.h>

namespace ioscheduler {

// Callback interface from Scheduler to client. Callbacks are made from within
// the Scheduler library to the client implementation. All callbacks are made
// with no locks held and are allowed to block. Any callbacks may be invoked
// simultanously, and one may be called multiple times concurrently, but never
// with the same data. Notably, Acquire(), Issue(), and Release() may be called
// multiple times after CancelAcquire() has been called.
class SchedulerClient {
public:
    // CanReorder
    //   Compare if ops can be reordered with respect to each other. This
    // function is called for every pair of ops whose position in
    // the stream is being considered for reorder relative to each other.
    // Returns:
    //   true if it is safe to reorder |second| ahead of |first|.
    //   false otherwise.
    virtual bool CanReorder(StreamOp* first, StreamOp* second) = 0;

    // Acquire
    //   Read zero or more ops from the client for intake into the
    // Scheduler. Every op obtained through Acquire will be returned to the client
    // via the Release callback. The Scheduler will never attempt to free these pointers.
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
    virtual zx_status_t Acquire(StreamOp** sop_list, size_t list_count, size_t* actual_count,
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
    virtual zx_status_t Issue(StreamOp* sop) = 0;

    // Release
    //   Yield ownership of the operation. The completion status of the op
    // is available in its |result| field. Once released, the Scheduler
    // maintains no references to the op and it can be safely deallocated or
    // reused.
    // Args:
    //   sop - op to be released.
    virtual void Release(StreamOp* sop) = 0;

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

} // namespace ioscheduler
