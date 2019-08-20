// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BLOBFS_WRITEBACK_H_
#define BLOBFS_WRITEBACK_H_

#include <lib/fit/optional.h>
#include <lib/fit/promise.h>
#include <lib/zx/vmo.h>

#include <utility>

#include <blobfs/journal/journal2.h>
#include <fbl/ref_ptr.h>
#include <fs/block-txn.h>
#include <fs/operation/buffered_operation.h>
#include <fs/operation/unbuffered_operations_builder.h>

namespace blobfs {

using fs::BufferedOperation;

// Wraps a promise with a reference to a ref-counted object.
//
// This keeps |object| alive until the promise completes or is abandoned.
template <typename Promise, typename T>
decltype(auto) wrap_reference(Promise promise, fbl::RefPtr<T> object) {
  return promise.then(
      [obj = std::move(object)](typename Promise::result_type& result) mutable { return result; });
}

// Flushes |operations| to persistent storage using a transaction created by |transaction_handler|,
// sending through the disk-registered |vmoid| object.
zx_status_t FlushWriteRequests(fs::TransactionHandler* transaction_handler,
                               const fbl::Vector<BufferedOperation>& operations);

}  // namespace blobfs

#endif  // BLOBFS_WRITEBACK_H_
