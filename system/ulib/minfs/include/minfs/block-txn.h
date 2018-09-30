// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/algorithm.h>
#include <fbl/macros.h>

#include <fs/block-txn.h>

#include <minfs/bcache.h>
#include <minfs/format.h>

namespace minfs {

#ifdef __Fuchsia__

struct WriteRequest {
    zx_handle_t vmo;
    size_t vmo_offset;
    size_t dev_offset;
    size_t length;
};

// A transaction consisting of enqueued VMOs to be written
// out to disk at specified locations.
//
// TODO(smklein): Rename to LogWriteTxn, or something similar, to imply
// that this write transaction acts fundamentally different from the
// ulib/fs WriteTxn under the hood.
class WriteTxn {
public:
    DISALLOW_COPY_ASSIGN_AND_MOVE(WriteTxn);
    explicit WriteTxn(Bcache* bc) : bc_(bc) {}
    ~WriteTxn() {
        ZX_DEBUG_ASSERT_MSG(requests_.size() == 0, "WriteTxn still has pending requests");
    }

    // Identify that a block should be written to disk at a later point in time.
    void Enqueue(zx_handle_t vmo, uint64_t vmo_offset, uint64_t dev_offset, uint64_t nblocks);

    fbl::Vector<WriteRequest>& Requests() { return requests_; }

    size_t BlkCount() const;

protected:
    // Activate the transaction, writing it out to disk.
    //
    // Each transaction uses the |vmo| / |vmoid| pair supplied, since the
    // transactions should be all reading from a single in-memory buffer.
    zx_status_t Flush(zx_handle_t vmo, vmoid_t vmoid);

private:
    Bcache* bc_;
    fbl::Vector<WriteRequest> requests_;
};

#else

using WriteTxn = fs::WriteTxn;

#endif

} // namespace minfs
