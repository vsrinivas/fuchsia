// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <blobfs/writeback-work.h>

#include <utility>

namespace blobfs {

void WritebackWork::MarkCompleted(zx_status_t status) {
    transaction_.Reset();
    if (sync_cb_) {
        sync_cb_(status);
    }
    sync_cb_ = nullptr;
    ready_cb_ = nullptr;
}

bool WritebackWork::IsReady() {
    if (ready_cb_) {
        if (ready_cb_()) {
            ready_cb_ = nullptr;
            return true;
        }

        return false;
    }

    return true;
}

void WritebackWork::SetReadyCallback(ReadyCallback callback) {
    ZX_DEBUG_ASSERT(!ready_cb_);
    ready_cb_ = std::move(callback);
}

void WritebackWork::SetSyncCallback(SyncCallback callback) {
    if (sync_cb_) {
        // This "callback chain" allows multiple clients to observe the completion
        // of the WritebackWork. This is akin to a promise "and-then" relationship.
        sync_cb_ = [previous_callback = std::move(sync_cb_),
                    next_callback = std::move(callback)] (zx_status_t status) {
            next_callback(status);
            previous_callback(status);
        };
    } else {
        sync_cb_ = std::move(callback);
    }
}

// Returns the number of blocks of the writeback buffer that have been consumed
zx_status_t WritebackWork::Complete() {
    zx_status_t status = transaction_.Flush();
    MarkCompleted(status);
    return status;
}

WritebackWork::WritebackWork(TransactionManager* transaction_manager)
    : transaction_(transaction_manager), ready_cb_(nullptr), sync_cb_(nullptr) {}

} // namespace blobfs
