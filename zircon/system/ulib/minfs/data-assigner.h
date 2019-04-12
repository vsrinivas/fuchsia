// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#ifndef __Fuchsia__
#error "Fuchsia-only header"
#endif

#include <fbl/condition_variable.h>
#include <fbl/function.h>
#include <fbl/intrusive_single_list.h>
#include <minfs/writeback.h>
#include "allocator/allocator.h"

#include <optional>

// The maximum number of tasks that can be enqueued at a time.
constexpr uint32_t kMaxQueued = 16;

namespace minfs {

class TransactionalFs;

using TaskCallback = fbl::Function<void(TransactionalFs* minfs)>;

// A generic, circular buffer of tasks.
//
// This class is not assignable, copyable, or moveable.
//
// TODO: Rename. The interface has nothing to do with "data blocks" or "assignment".
class DataBlockAssigner {
public:
    static zx_status_t Create(TransactionalFs* minfs, fbl::unique_ptr<DataBlockAssigner>* out);

    DataBlockAssigner() = default;
    DataBlockAssigner(const DataBlockAssigner&) = delete;
    DataBlockAssigner(DataBlockAssigner&&) = delete;
    DataBlockAssigner& operator=(const DataBlockAssigner&) = delete;
    DataBlockAssigner& operator=(DataBlockAssigner&&) = delete;
    ~DataBlockAssigner();

    // Enqueue a unit of work to be processed by in a background thread.
    //
    // This operation is thread-safe.
    void EnqueueCallback(TaskCallback task);

    // Returns true if any tasks are waiting for resources to become available.
    bool TasksWaiting() const;

private:
    DataBlockAssigner(TransactionalFs* minfs) : minfs_(minfs) {}

    bool IsEmpty() const __TA_REQUIRES(lock_);

    // Processes next element in the queue. Requires at least one element to exist in the queue.
    void ProcessNext() __TA_REQUIRES(lock_);

    // Reserves and returns the next task in the queue. If the queue is full (count == kMaxQueued),
    // blocks until a slot becomes available.
    void ReserveTask(TaskCallback task) __TA_REQUIRES(lock_);

    // If the queue is full, sync until at least one task becomes available.
    void EnsureQueueSpace() __TA_REQUIRES(lock_);

    // Loop which asynchronously processes transactions.
    void ProcessLoop();

    // Thread used exclusively to run the ProcessLoop.
    static int DataThread(void* arg);

    mutable fbl::Mutex lock_; // Lock required for all variables accessed in the async thread.

    // A circular buffer acting as a queue for DataTasks waiting to be processed. |start_|
    // indicates the index of the first task in the queue, and |count_| indicates the total number
    // of tasks waiting in the queue.
    std::optional<TaskCallback> task_queue_[kMaxQueued] __TA_GUARDED(lock_);

    uint32_t start_ __TA_GUARDED(lock_) = 0;
    uint32_t count_ __TA_GUARDED(lock_) = 0;

    bool unmounting_ __TA_GUARDED(lock_) = false;

    // The number of tasks currently waiting for space in the queue to become available.
    uint32_t waiting_ __TA_GUARDED(lock_) = 0;

    TransactionalFs* minfs_;

    std::optional<thrd_t> thrd_; // Thread which periodically updates all pending data allocations.
    fbl::ConditionVariable data_cvar_; // Signalled when the queue has tasks ready to complete.
    fbl::ConditionVariable sync_cvar_; // Signalled when the queue size decreases from max capacity.
};
} //namespace minfs
