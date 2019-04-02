// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/zx/time.h>

namespace async {

// An abstract class responsible for dispatching timer expirations.
class TimerDispatcher {
public:
    virtual ~TimerDispatcher() = default;

    // Signal that a timer has expired.
    virtual void FireTimer() = 0;
};

// An abstract class that tells time and registers timers to signal expirations
// with the provided callbacks.
class TimeKeeper {
public:
    virtual ~TimeKeeper() = default;

    // Returns the current time.
    virtual zx::time Now() const = 0;
};

} // namespace
