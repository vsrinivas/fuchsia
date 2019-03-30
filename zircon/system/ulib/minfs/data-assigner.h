// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#ifndef __Fuchsia__
#error "Fuchsia-only header"
#endif

#include <fbl/intrusive_single_list.h>
#include <minfs/writeback.h>
#include "allocator/allocator.h"

#include <optional>

using SyncCallback = fs::Vnode::SyncCallback;

namespace minfs {

class DataAssignableVnode;
class VnodeMinfs;

// Represents a data block allocation task to be processed by the DataBlockAssigner.
// This class is moveable, but not copyable or assignable.
class DataTask {
public:
    // Initialize an invalid task.
    DataTask() = default;
    // Initializes the task with |vnode| which needs data blocks to be assigned.
    DataTask(fbl::RefPtr<DataAssignableVnode> vnode);
    DataTask(const DataTask&) = delete;
    DataTask(DataTask&& other) = default;
    DataTask& operator=(const DataTask&) = delete;
    DataTask& operator=(DataTask&& other) = default;
    ~DataTask() = default;

    // Uses |transaction| to process the task.
    zx_status_t Process(Transaction* transaction);

private:
    fbl::RefPtr<DataAssignableVnode> vnode_; // Vnode to resolve copy-on-write blocks.
};

// Asynchronously processes pending DataTasks.
// This class is not assignable, copyable, or moveable.
class DataBlockAssigner {
public:
    DataBlockAssigner() = default;
    DataBlockAssigner(const DataBlockAssigner&) = delete;
    DataBlockAssigner(DataBlockAssigner&&) = delete;
    DataBlockAssigner& operator=(const DataBlockAssigner&) = delete;
    DataBlockAssigner& operator=(DataBlockAssigner&&) = delete;
    ~DataBlockAssigner();

    // Enqueues a Vnode to be updated. This may only be invoked once until a call to Process() is
    // made.
    void EnqueueAllocation(fbl::RefPtr<DataAssignableVnode> vnode);

    // Processes |task_| by allocating any pending data blocks. If no allocation task has been
    // enqueued, no action is taken.
    void Process(Transaction* transaction);

private:
    std::optional<DataTask> task_;
};
} //namespace minfs