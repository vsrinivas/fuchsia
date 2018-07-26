// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <zircon/device/block.h>
#include <ddk/protocol/block.h>
#include <zircon/thread_annotations.h>
#include <zircon/types.h>

#include <fbl/atomic.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/intrusive_wavl_tree.h>
#include <fbl/mutex.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>
#include <lib/zx/fifo.h>
#include <lib/zx/vmo.h>
#include <lib/sync/completion.h>

// Should a reponse be sent when we hit ctr?
constexpr uint32_t kTxnFlagRespond = 0x00000001;

// TODO(ZX-1586): Reduce the locking of TransactionGroup.
class TransactionGroup {
public:
    TransactionGroup();
    ~TransactionGroup();
    // Initialize must be called before utilizing other functions in
    // TransactionGroup. Initialize should only be called once.
    void Initialize(zx_handle_t fifo, groupid_t group) TA_NO_THREAD_SAFETY_ANALYSIS;

    // Verifies that the incoming txn does not break the Block IO fifo protocol.
    // If it is successful, sets up the response_ with the registered cookie,
    // and adds to the "ctr_" counter of number of Completions that must be
    // received before the transaction is identified as successful.
    zx_status_t Enqueue(bool do_respond, reqid_t reqid) TA_EXCL(lock_);

    // Add |n| to the number of completions we expect to receive before
    // responding to this txn.
    void CtrAdd(uint32_t n) TA_EXCL(lock_);

    // Called once the transaction has completed successfully.
    // This function may respond on the fifo, resetting |response_|.
    void Complete(zx_status_t status) TA_EXCL(lock_);
private:
    DISALLOW_COPY_ASSIGN_AND_MOVE(TransactionGroup);

    // Should only be set once.
    zx_handle_t fifo_;

    fbl::Mutex lock_;
    block_fifo_response_t response_ TA_GUARDED(lock_); // The response to be sent back to the client
    uint32_t flags_ TA_GUARDED(lock_);
    uint32_t ctr_ TA_GUARDED(lock_); // How many ops does the block device need to complete?
};
