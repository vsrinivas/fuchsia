// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cobalt-client/cpp/in-memory-logger.h>

namespace cobalt_client {

InMemoryLogger::~InMemoryLogger() {}

// Adds the contents of buckets and the required info to a buffer.
bool InMemoryLogger::Log(const RemoteMetricInfo& remote_info, const HistogramBucket* buckets,
                         size_t num_buckets) {
    auto& hist_buckets = persisted_histograms_[remote_info.metric_id];
    for (size_t bucket = 0; bucket < num_buckets; ++bucket) {
        hist_buckets[buckets[static_cast<uint32_t>(bucket)].index] += buckets[bucket].count;
    }
    return true;
}

// Adds the count and the required info to a buffer.
bool InMemoryLogger::Log(const RemoteMetricInfo& remote_info, int64_t count) {
    persisted_counters_[remote_info.metric_id] += count;
    return true;
}

} // namespace cobalt_client
