// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cobalt-client/cpp/histogram.h>
#include <lib/fzl/time.h>
#include <lib/zx/time.h>
#include <zircon/assert.h>

namespace cobalt_client {

// RAII Timer.
//
// This class is moveable, but not copyable or assignable.
//
// void InterestingFunction() {
//    cobalt_client::Timer timer(my_interesting_histogram_, is_collecting_);
//    ...;
// }
class Timer {
public:
    // Returns the amount of nanoseconds that a |zx::ticks| represent. It is used as the
    // default for |ticks_to_units_| in a |Timer|.
    static int64_t TicksToNs(zx::ticks ticks) { return fzl::TicksToNs(ticks).to_nsecs(); }

    Timer(Histogram metric, bool is_collecting, int64_t (*ticks_to_unit)(zx::ticks) = TicksToNs)
        : metric_(metric), ticks_to_unit_(ticks_to_unit),
          start_(is_collecting ? zx::ticks::now().get() : 0) {
        ZX_DEBUG_ASSERT(ticks_to_unit_ != nullptr);
    }

    Timer(const Timer&) = delete;
    Timer(Timer&& other)
        : metric_(other.metric_), ticks_to_unit_(other.ticks_to_unit_), start_(other.start_) {
        other.Cancel();
    }

    Timer& operator=(const Timer&) = delete;
    Timer& operator=(Timer&& other) = delete;
    ~Timer() { End(); }

    // Stops the timer and logs the duration.
    void End() {
        if (start_.get() > 0) {
            int64_t delta = ticks_to_unit_(zx::ticks::now() - start_);
            ZX_DEBUG_ASSERT(delta >= 0);
            metric_.Add(static_cast<uint64_t>(delta));
            start_ = zx::ticks(0);
        }
    }

    // Prevents the timer from logging any duration.
    void Cancel() { start_ = zx::ticks(0); }

private:
    Histogram metric_;
    int64_t (*ticks_to_unit_)(zx::ticks);
    zx::ticks start_;
};

} // namespace cobalt_client
