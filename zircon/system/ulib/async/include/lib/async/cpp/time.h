// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ASYNC_CPP_TIME_H_
#define LIB_ASYNC_CPP_TIME_H_

#include <lib/async/time.h>
#include <lib/zx/time.h>

namespace async {

// Returns the current time in the dispatcher's timebase.
// For most loops, this is generally obtained from |ZX_CLOCK_MONOTONIC|
// but certain loops may use a different timebase, notably for testing.
inline zx::time Now(async_dispatcher_t* dispatcher) { return zx::time(async_now(dispatcher)); }

}  // namespace async

#endif  // LIB_ASYNC_CPP_TIME_H_
