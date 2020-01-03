// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FS_TRANSACTION_WRITEBACK_H_
#define FS_TRANSACTION_WRITEBACK_H_

#include <lib/fit/optional.h>
#include <lib/fit/promise.h>
#include <lib/zx/vmo.h>

#include <utility>
#include <vector>

#include <fbl/ref_ptr.h>
#include <fbl/vector.h>
#include <fs/transaction/block_transaction.h>
#include <storage/operation/buffered_operation.h>
#include <storage/operation/unbuffered_operations_builder.h>

namespace fs {

// Wraps a promise with a reference to a ref-counted object.
//
// This keeps |object| alive until the promise completes or is abandoned.
template <typename Promise, typename T>
decltype(auto) wrap_reference(Promise promise, fbl::RefPtr<T> object) {
  return promise.then(
      [obj = std::move(object)](typename Promise::result_type& result) mutable { return result; });
}

// Wraps a promise with a vector of references to ref-counted objects.
//
// This keeps all objects in |object_vector| alive until the promise completes or is abandoned.
template <typename Promise, typename T>
decltype(auto) wrap_reference_vector(Promise promise, std::vector<fbl::RefPtr<T>> object_vector) {
  return promise.then([obj = std::move(object_vector)](
                          typename Promise::result_type& result) mutable { return result; });
}

// Flushes |operations| to persistent storage using a transaction created by |transaction_handler|,
// sending through the disk-registered |vmoid| object.
zx_status_t FlushRequests(TransactionHandler* transaction_handler,
                          const fbl::Vector<storage::BufferedOperation>& operations);

}  // namespace fs

#endif  // FS_TRANSACTION_WRITEBACK_H_
