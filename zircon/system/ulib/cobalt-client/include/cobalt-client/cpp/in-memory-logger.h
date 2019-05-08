// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <map>

#include <cobalt-client/cpp/counter.h>
#include <cobalt-client/cpp/histogram.h>
#include <cobalt-client/cpp/types-internal.h>

namespace cobalt_client {

// This class provides an in memory implementation of the logger, that persists data to a map.
// This is intended to be used in tests, so that users can verify things are logged correctly, by
// instantiating the collector through Collector(InMemoryLogger());
// The class intentionally virtual and not final, to allow customization by unit tests.
class InMemoryLogger : public internal::Logger {
public:
    using RemoteMetricInfo = internal::RemoteMetricInfo;
    using HistogramBucket = internal::HistogramBucket;

    InMemoryLogger() = default;
    InMemoryLogger(const InMemoryLogger&) = delete;
    InMemoryLogger(InMemoryLogger&&) = delete;
    InMemoryLogger& operator=(const InMemoryLogger&) = delete;
    InMemoryLogger& operator=(InMemoryLogger&&) = delete;
    ~InMemoryLogger() override;

    // Adds the contents of buckets and the required info to a buffer.
    bool Log(const RemoteMetricInfo& remote_info, const HistogramBucket* buckets,
             size_t num_buckets) override;

    // Adds the count and the required info to a buffer.
    bool Log(const RemoteMetricInfo& remote_info, int64_t count) override;

    const std::map<uint64_t, Counter::Count>& counters() const { return persisted_counters_; }

    const std::map<uint64_t, std::map<uint32_t, Histogram<1>::Count>>& histograms() const {
        return persisted_histograms_;
    }

protected:
    std::map<uint64_t, Counter::Count> persisted_counters_;
    std::map<uint64_t, std::map<uint32_t, Histogram<1>::Count>> persisted_histograms_;
};

} // namespace cobalt_client
