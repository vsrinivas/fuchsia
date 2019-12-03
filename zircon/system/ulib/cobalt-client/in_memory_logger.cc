// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cobalt-client/cpp/in_memory_logger.h>

namespace cobalt_client {

InMemoryLogger::~InMemoryLogger() {}

// Adds the contents of buckets and the required info to a buffer.
bool InMemoryLogger::Log(const MetricOptions& metric_info, const HistogramBucket* buckets,
                         size_t num_buckets) {
  if (fail_logging_) {
    return false;
  }

  auto& hist_buckets = persisted_histograms_[metric_info];
  for (size_t bucket = 0; bucket < num_buckets; ++bucket) {
    hist_buckets[buckets[static_cast<uint32_t>(bucket)].index] += buckets[bucket].count;
  }
  return true;
}

// Adds the count and the required info to a buffer.
bool InMemoryLogger::Log(const MetricOptions& metric_info, int64_t count) {
  if (fail_logging_) {
    return false;
  }
  persisted_counters_[metric_info] += count;
  return true;
}

}  // namespace cobalt_client
