// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_CLIENT_CPP_IN_MEMORY_LOGGER_H_
#define COBALT_CLIENT_CPP_IN_MEMORY_LOGGER_H_

#include <map>

#include <cobalt-client/cpp/counter.h>
#include <cobalt-client/cpp/histogram.h>
#include <cobalt-client/cpp/types_internal.h>

namespace cobalt_client {

// This class provides an in memory implementation of the logger, that persists data to a map.
// This is intended to be used in tests, so that users can verify things are logged correctly, by
// instantiating the collector through Collector(InMemoryLogger());
// The class intentionally virtual and not final, to allow customization by unit tests.
class InMemoryLogger : public internal::Logger {
 public:
  using HistogramBucket = internal::HistogramBucket;
  template <typename MetricType>
  using MetricMap = std::map<MetricOptions, MetricType, MetricOptions::LessThan>;
  using HistogramStorage = std::map<uint32_t, Histogram<1>::Count>;

  InMemoryLogger() = default;
  InMemoryLogger(const InMemoryLogger&) = delete;
  InMemoryLogger(InMemoryLogger&&) = delete;
  InMemoryLogger& operator=(const InMemoryLogger&) = delete;
  InMemoryLogger& operator=(InMemoryLogger&&) = delete;
  ~InMemoryLogger() override;

  // Adds the contents of buckets and the required info to a buffer.
  bool Log(const MetricOptions& metric_info, const HistogramBucket* buckets,
           size_t num_buckets) override;

  // Adds the count and the required info to a buffer.
  bool Log(const MetricOptions& metric_info, int64_t count) override;

  const MetricMap<Counter::Count>& counters() const { return persisted_counters_; }

  const MetricMap<HistogramStorage>& histograms() const { return persisted_histograms_; }

  void fail_logging(bool fail) { fail_logging_ = fail; }

 protected:
  bool fail_logging_ = false;
  MetricMap<Counter::Count> persisted_counters_;
  MetricMap<HistogramStorage> persisted_histograms_;
};

}  // namespace cobalt_client

#endif  // COBALT_CLIENT_CPP_IN_MEMORY_LOGGER_H_
