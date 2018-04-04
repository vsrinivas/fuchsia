// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains a helper for gathering metrics timing info.

#pragma once

#ifdef __Fuchsia__
#include <lib/zx/time.h>
#endif

// Compile-time option to enable metrics collection globally. On by default.
#define ENABLE_METRICS

#if defined(__Fuchsia__) && defined(ENABLE_METRICS)
#define FS_WITH_METRICS
#endif

namespace fs {

#ifdef FS_WITH_METRICS

// Helper class for getting the duration of events.
typedef zx::ticks Duration;

class Ticker {
public:
    explicit Ticker(bool collecting_metrics)
            : ticks_(collecting_metrics ? zx::ticks::now() : zx::ticks()) {}

    void Reset() {
        if (ticks_.get() == 0) {
            return;
        }
        ticks_ = zx::ticks::now();
    }

    // Returns '0' for duration if collecting_metrics is false,
    // preventing an unnecessary syscall.
    //
    // Otherwise, returns the time since either the constructor
    // or the last call to reset (whichever was more recent).
    Duration End() const {
        if (ticks_.get() == 0) {
            return zx::ticks();
        }
        return zx::ticks::now() - ticks_;
    }
private:
    zx::ticks ticks_;
};

#else

// Null implementation for host-side code.
class Duration {};

class Ticker {
public:
    Ticker(bool) {}
    void Reset();
    Duration End() const {
        return Duration();
    }
};

#endif

} // namespace fs
