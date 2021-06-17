// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(78633): Remove stub header when users are ported to the fpromise namespace and include path.

#ifndef LIB_FIT_PROMISE_INCLUDE_LIB_FIT_SCHEDULER_H_
#define LIB_FIT_PROMISE_INCLUDE_LIB_FIT_SCHEDULER_H_

#include <lib/fpromise/scheduler.h>
#include <lib/fit/promise.h>

namespace fit {
namespace subtle {

using fpromise::subtle::scheduler;

}  // namespace subtle
}  // namespace fit

#endif  // LIB_FIT_PROMISE_INCLUDE_LIB_FIT_SCHEDULER_H_
