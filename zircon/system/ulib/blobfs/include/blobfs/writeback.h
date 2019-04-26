// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#ifndef __Fuchsia__
#error Fuchsia-only Header
#endif

#include <utility>

#include <blobfs/transaction-manager.h>
#include <blobfs/writeback-work.h>
#include <fbl/ref_ptr.h>
#include <lib/zx/vmo.h>

namespace blobfs {

// An object compatible with the WritebackWork interface, which contains a single blob reference.
// When the writeback is completed, this reference will go out of scope.
//
// This class helps WritebackWork avoid concurrent writes and reads to blobs: if a BlobWork
// is alive, the impacted Blob is still alive.
class BlobWork : public WritebackWork {
public:
    BlobWork(TransactionManager* transaction_manager, fbl::RefPtr<Blob> vnode)
        : WritebackWork(transaction_manager), vnode_(std::move(vnode)) {}

private:
    fbl::RefPtr<Blob> vnode_;
};

// A wrapper around "Enqueue" for content which risks being larger
// than the writeback buffer.
//
// For content which is smaller than 3/4 the size of the writeback buffer: the
// content is enqueued to |work| without flushing.
//
// For content which is larger than 3/4 the size of the writeback buffer: flush
// the data by enqueueing it to the writeback thread in chunks until the
// remainder is small enough to comfortably fit within the writeback buffer.
zx_status_t EnqueuePaginated(std::unique_ptr<WritebackWork>* work,
                             TransactionManager* transaction_manager, Blob* vn,
                             const zx::vmo& vmo, uint64_t relative_block, uint64_t absolute_block,
                             uint64_t nblocks);

} // namespace blobfs
