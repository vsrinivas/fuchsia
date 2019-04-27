// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#ifndef __Fuchsia__
#error Fuchsia-only Header
#endif

#include <utility>

#include <blobfs/transaction-manager.h>
#include <blobfs/write-txn.h>
#include <fbl/function.h>
#include <fbl/intrusive_single_list.h>
#include <fs/vfs.h>

namespace blobfs {

// A wrapper around a WriteTxn with added support for callback invocation on completion.
class WritebackWork : public fbl::SinglyLinkedListable<std::unique_ptr<WritebackWork>> {
public:
    using ReadyCallback = fbl::Function<bool()>;
    using SyncCallback = fs::Vnode::SyncCallback;

    WritebackWork(TransactionManager* transaction_manager);
    virtual ~WritebackWork() = default;

    // Sets the WritebackWork to a completed state. |status| should indicate whether the work was
    // completed successfully.
    void MarkCompleted(zx_status_t status);

    // Returns true if the WritebackWork is "ready" to be processed. This is always true unless a
    // "ready callback" exists, in which case that callback determines the state of readiness. Once
    // a positive response is received, the ready callback is destroyed - the WritebackWork will
    // always be ready from this point forward.
    bool IsReady();

    // Adds a callback to the WritebackWork to be called before the WritebackWork is completed,
    // to ensure that it's ready for writeback.
    //
    // Only one ready callback may be set for each WritebackWork unit.
    void SetReadyCallback(ReadyCallback callback);

    // Adds a callback to the WritebackWork, such that it will be signalled when the WritebackWork
    // is flushed to disk. If no callback is set, nothing will get signalled.
    //
    // Multiple callbacks may be set. They are invoked in "first-in, last-out" order (i.e.,
    // enqueueing A, B, C will invoke C, B, A).
    void SetSyncCallback(SyncCallback callback);

    // Persists the enqueued work to disk,
    // and resets the WritebackWork to its initial state.
    zx_status_t Complete();

    WriteTxn& Transaction() { return transaction_; }

private:
    WriteTxn transaction_;
    // Optional callbacks.
    ReadyCallback ready_cb_; // Call to check whether work is ready to be processed.
    SyncCallback sync_cb_; // Call after work has been completely flushed.
};

} // namespace blobfs
