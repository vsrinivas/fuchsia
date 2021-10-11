// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/timekeeper/async_test_clock.h"

#include <lib/async/time.h>

namespace timekeeper {
namespace {

fit::function<zx_time_t()> GetTimeFactory(async_dispatcher_t* dispatcher) {
  return [dispatcher] { return async_now(dispatcher); };
}

}  // namespace

AsyncTestClock::AsyncTestClock(async_dispatcher_t* dispatcher)
    : MonotonicTestClockBase(GetTimeFactory(dispatcher)) {}

AsyncTestClock::~AsyncTestClock() = default;

}  // namespace timekeeper
