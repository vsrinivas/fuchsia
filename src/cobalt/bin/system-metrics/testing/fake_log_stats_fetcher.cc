// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/system-metrics/testing/fake_log_stats_fetcher.h"

#include <lib/async/cpp/task.h>

namespace cobalt {

FakeLogStatsFetcher::FakeLogStatsFetcher(async_dispatcher_t* dispatcher)
    : dispatcher_(dispatcher) {}

void FakeLogStatsFetcher::AddErrorCount(int error_count) { error_count_ += error_count; }

void FakeLogStatsFetcher::FetchMetrics(MetricsCallback metrics_callback) {
  metrics_callback_ = std::move(metrics_callback);
  async::PostTask(dispatcher_, [this]() {
    LogStatsFetcher::Metrics metrics;
    metrics.error_count = error_count_;
    bool res = metrics_callback_(metrics);
    if (res) {
      error_count_ = 0;
    }
  });
}

}  // namespace cobalt
