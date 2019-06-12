// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ASYNC_TESTING_TIME_KEEPER_H_
#define LIB_ASYNC_TESTING_TIME_KEEPER_H_

#include <lib/zx/time.h>

namespace async {

// An abstract class that tells time and registers timers to signal expirations
// with the provided callbacks.
class TimeKeeper {
public:
    virtual ~TimeKeeper() = default;

    // Returns the current time.
    virtual zx::time Now() const = 0;
};

} // namespace async

#endif // LIB_ASYNC_TESTING_TIME_KEEPER_H_
