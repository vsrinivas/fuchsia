// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#ifndef __Fuchsia__
#error Fuchsia-only Header
#endif

#include <cobalt-client/cpp/timer.h>
#include <fs/metrics.h>
#include <fs/vnode.h>

#include <utility>

namespace blobfs {

// RAII interface for registering latency events.
class LatencyEvent {
public:
    LatencyEvent(cobalt_client::Histogram<fs::VnodeMetrics::kHistogramBuckets>* histogram,
                 bool collect)
        : timer_(collect), histogram_(histogram) {}
    LatencyEvent(LatencyEvent&& rhs)
        : timer_(std::move(rhs.timer_)), histogram_(std::move(rhs.histogram_)) {}
    ~LatencyEvent() {
        zx::duration latency = timer_.End();
        if (latency.get() > 0) {
            ZX_DEBUG_ASSERT(histogram_ != nullptr);
            histogram_->Add(latency.get());
        }
    }

private:
    cobalt_client::Timer timer_;
    cobalt_client::Histogram<fs::VnodeMetrics::kHistogramBuckets>* histogram_;
};

} // namespace blobfs
