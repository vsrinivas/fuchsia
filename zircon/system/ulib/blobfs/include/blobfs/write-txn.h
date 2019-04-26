// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#ifndef __Fuchsia__
#error Fuchsia-only Header
#endif

#include <utility>

#include <blobfs/transaction-manager.h>
#include <fbl/vector.h>
#include <lib/zx/vmo.h>

namespace blobfs {

struct WriteRequest {
    zx_handle_t vmo;
    size_t vmo_offset;
    size_t dev_offset;
    size_t length;
};

// A transaction consisting of enqueued VMOs to be written
// out to disk at specified locations.
class WriteTxn {
public:
    DISALLOW_COPY_ASSIGN_AND_MOVE(WriteTxn);

    explicit WriteTxn(TransactionManager* transaction_manager)
        : transaction_manager_(transaction_manager), vmoid_(VMOID_INVALID), block_count_(0) {}

    virtual ~WriteTxn();

    // Identifies that |nblocks| blocks of data starting at |relative_block| within the |vmo|
    // should be written out to |absolute_block| on disk at a later point in time.
    void Enqueue(const zx::vmo& vmo, uint64_t relative_block, uint64_t absolute_block,
                 uint64_t nblocks);

    fbl::Vector<WriteRequest>& Requests() { return requests_; }

    // Returns the first block at which this WriteTxn exists within its VMO buffer.
    // Requires all requests within the transaction to have been copied to a single buffer.
    size_t BlkStart() const;

    // Returns the total number of blocks in all requests within the WriteTxn. This number is
    // calculated at call time, unless the WriteTxn has already been fully buffered, at which point
    // the final |block_count_| is set. This is then returned for all subsequent calls to BlkCount.
    size_t BlkCount() const;

    bool IsBuffered() const {
        return vmoid_ != VMOID_INVALID;
    }

    // Sets the source buffer for the WriteTxn to |vmoid|.
    void SetBuffer(vmoid_t vmoid);

    // Checks if the WriteTxn vmoid_ matches |vmoid|.
    bool CheckBuffer(vmoid_t vmoid) const {
        return vmoid_ == vmoid;
    }

    // Resets the transaction's state.
    void Reset() {
        requests_.reset();
        vmoid_ = VMOID_INVALID;
    }

protected:
    // Activates the transaction.
    zx_status_t Flush();

private:
    TransactionManager* transaction_manager_;
    vmoid_t vmoid_;
    fbl::Vector<WriteRequest> requests_;
    size_t block_count_;
};

} // namespace blobfs
