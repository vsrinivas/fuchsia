// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/system-metrics/testing/fake_log_stats_fetcher.h"

#include <lib/async/cpp/task.h>

namespace cobalt {

FakeLogStatsFetcher::FakeLogStatsFetcher(async_dispatcher_t* dispatcher)
    : dispatcher_(dispatcher) {}

void FakeLogStatsFetcher::AddErrorCount(int error_count) {
  pending_metrics_.error_count += error_count;
}

void FakeLogStatsFetcher::AddKlogCount(int klog_count) {
  pending_metrics_.klog_count += klog_count;
}

void FakeLogStatsFetcher::AddGranularRecord(const std::string& file_path, uint64_t line_no,
                                            uint64_t count) {
  pending_metrics_.granular_stats.emplace_back(file_path, line_no, count);
}

void FakeLogStatsFetcher::AddComponentErrorCount(ComponentEventCode component_id,
                                                 uint64_t error_count) {
  pending_metrics_.per_component_error_count[component_id] += error_count;
}

void FakeLogStatsFetcher::FetchMetrics(MetricsCallback metrics_callback) {
  metrics_callback_ = std::move(metrics_callback);
  async::PostTask(dispatcher_, [this]() {
    metrics_callback_(pending_metrics_);
    pending_metrics_ = Metrics();
  });
}

}  // namespace cobalt
