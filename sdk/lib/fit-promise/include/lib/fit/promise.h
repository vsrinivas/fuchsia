// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(78633): Remove stub header when users are ported to the fpromise namespace and include path.

#ifndef LIB_FIT_PROMISE_INCLUDE_LIB_FIT_PROMISE_H_
#define LIB_FIT_PROMISE_INCLUDE_LIB_FIT_PROMISE_H_

#include <lib/fpromise/promise.h>
#include <lib/fit/result.h>

namespace fit {

using fpromise::promise;
using fpromise::swap;
using fpromise::make_promise_with_continuation;
using fpromise::make_promise;
using fpromise::make_result_promise;
using fpromise::make_ok_promise;
using fpromise::make_error_promise;
using fpromise::join_promises;
using fpromise::join_promise_vector;
using fpromise::future_state;
using fpromise::future;
using fpromise::make_future;
using fpromise::pending_task;
using fpromise::context;
using fpromise::executor;
using fpromise::suspended_task;

using fpromise::promise_impl;

namespace internal {

using fpromise::internal::result_continuation;

}  // namespace internal

}  // namespace fit

#endif // LIB_FIT_PROMISE_INCLUDE_LIB_FIT_PROMISE_H_
