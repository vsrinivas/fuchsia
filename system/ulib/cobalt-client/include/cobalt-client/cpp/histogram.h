// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

#include <cobalt-client/cpp/collector.h>
#include <cobalt-client/cpp/histogram-internal.h>
#include <cobalt-client/cpp/metric-options.h>
#include <cobalt-client/cpp/types-internal.h>

namespace cobalt_client {

// Thin wrapper for a histogram. This class does not own the data, but acts as a proxy.
//
// This class is not copyable, moveable or assignable.
// This class is thread-safe.
template <uint32_t num_buckets>
class Histogram {
public:
    static_assert(num_buckets > 0, "num_buckets must be postive.");

    // Underlying type used for representing bucket count.
    using Count = uint64_t;

    Histogram() = delete;
    Histogram(const HistogramOptions& options)
        : remote_histogram_(internal::RemoteMetricInfo::From(options)), options_(options) {}
    // Collector's lifetime must exceed the histogram's lifetime.
    Histogram(const HistogramOptions& options, Collector* collector)
        : remote_histogram_(internal::RemoteMetricInfo::From(options)), options_(options) {
        collector_ = collector;
        if (collector_ != nullptr) {
            collector_->Subscribe(&remote_histogram_);
        }
    }
    // Constructor for internal use only.
    Histogram(const HistogramOptions& options, internal::FlushInterface** flush_interface)
        : remote_histogram_(internal::RemoteMetricInfo::From(options)), options_(options) {
        *flush_interface = &remote_histogram_;
    }
    Histogram(const Histogram&) = delete;
    Histogram(Histogram&& other) = delete;
    Histogram& operator=(const Histogram&) = delete;
    Histogram& operator=(Histogram&&) = delete;
    ~Histogram() {
        if (collector_ != nullptr) {
            collector_->UnSubscribe(&remote_histogram_);
        }
    }

    // Returns the number of buckets allocated for this histogram. This includes
    // the overflow and underflow buckets.
    constexpr uint32_t size() const { return num_buckets + 2; }

    // Increases the count of the bucket containing |value| by |times|.
    // |ValueType| must either be an (u)int or a double.
    template <typename ValueType>
    void Add(ValueType value, Count times = 1) {
        double dbl_value = static_cast<double>(value);
        uint32_t bucket = options_.map_fn(dbl_value, size(), options_);
        remote_histogram_.IncrementCount(bucket, times);
    }

    // Returns the count of the bucket containing |value|, since it was last sent
    // to cobalt.
    // |ValueType| must either be an (u)int or a double.
    template <typename ValueType>
    Count GetRemoteCount(ValueType value) const {
        double dbl_value = static_cast<double>(value);
        uint32_t bucket = options_.map_fn(dbl_value, size(), options_);
        return remote_histogram_.GetCount(bucket);
    }

private:
    // Two extra buckets for overflow and underflow buckets.
    internal::RemoteHistogram<num_buckets + 2> remote_histogram_;

    HistogramOptions options_;

    Collector* collector_ = nullptr;
};
} // namespace cobalt_client
