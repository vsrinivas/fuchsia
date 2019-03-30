// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "data-assigner.h"
#include "minfs-private.h"

namespace minfs {

DataTask::DataTask(fbl::RefPtr<DataAssignableVnode> vnode) : vnode_(std::move(vnode)) {
    ZX_DEBUG_ASSERT(vnode_ != nullptr);
}

zx_status_t DataTask::Process(Transaction* transaction) {
    ZX_DEBUG_ASSERT(vnode_ != nullptr);
    vnode_->AllocateData(transaction);
    return ZX_OK;
}

DataBlockAssigner::~DataBlockAssigner() {
    ZX_DEBUG_ASSERT(!task_.has_value());
}

void DataBlockAssigner::EnqueueAllocation(fbl::RefPtr<DataAssignableVnode> vnode) {
    ZX_ASSERT(!task_.has_value());
    task_ = DataTask(std::move(vnode));
}

void DataBlockAssigner::Process(Transaction* transaction) {
    if (task_.has_value()) {
        std::optional<DataTask> task;
        task_.swap(task);
        task->Process(transaction);
    }
}

} // namespace minfs