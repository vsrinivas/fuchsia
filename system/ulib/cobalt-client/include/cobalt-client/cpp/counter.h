// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

#include <cobalt-client/cpp/collector.h>
#include <cobalt-client/cpp/counter-internal.h>
#include <cobalt-client/cpp/types-internal.h>

namespace cobalt_client {

// Thin wrapper for an atomic counter with a fixed memory order. The counter handles
// a remote count and a local count. The remote count is periodically flushed, while
// the local count is viewed on demand (and optionally flushed depending on configuration).
//
// This class is copyable, moveable and assignable.
// This class is thread-safe.
class Counter {
public:
    // Underlying type used for representing an actual counter.
    using Count = uint64_t;

    Counter() = delete;
    Counter(const MetricOptions& options)
        : remote_counter_(internal::RemoteMetricInfo::From(options)) {}
    Counter(const MetricOptions& options, Collector* collector)
        : remote_counter_(internal::RemoteMetricInfo::From(options)) {
        collector_ = collector;
        collector_->Subscribe(&remote_counter_);
    };
    // Constructor for internal use only.
    Counter(const MetricOptions& options, internal::FlushInterface** flush_interface)
        : remote_counter_(internal::RemoteMetricInfo::From(options)) {
        *flush_interface = &remote_counter_;
    };
    Counter(const Counter& other) = delete;
    Counter(Counter&&) = delete;
    ~Counter() {
        if (collector_ != nullptr) {
            collector_->UnSubscribe(&remote_counter_);
        }
    }

    // Increments the counter value by |value|. This applies to local and remote
    // values of the counter.
    void Increment(Count value = 1);

    // Returns the current value of the counter that would be
    // sent to the remote service(cobalt).
    Count GetRemoteCount() const;

private:
    internal::RemoteCounter remote_counter_;
    Collector* collector_ = nullptr;
};

} // namespace cobalt_client
