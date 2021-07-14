// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_COMMON_ASYNC_UTIL_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_COMMON_ASYNC_UTIL_H_

#include "lib/fpromise/promise.h"
#include "src/developer/debug/zxdb/common/err.h"

namespace zxdb {

// Helper for creating an error promise. This is used when the error is synchronously known in
// a function that returns a promise.
template <typename ResultType>
fpromise::promise<ResultType, Err> MakeErrPromise(Err err) {
  return fpromise::make_result_promise<ResultType, Err>(fpromise::error(std::move(err)));
}

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_COMMON_ASYNC_UTIL_H_
