// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/system-metrics/testing/fake_archivist_stats_fetcher.h"

#include <lib/async/cpp/task.h>

namespace cobalt {

FakeArchivistStatsFetcher::FakeArchivistStatsFetcher(async_dispatcher_t* dispatcher)
    : dispatcher_(dispatcher) {}

void FakeArchivistStatsFetcher::AddMeasurement(Measurement measurement) {
  measurements_.emplace_back(std::move(measurement));
}

void FakeArchivistStatsFetcher::FetchMetrics(MetricsCallback metrics_callback) {
  async::PostTask(dispatcher_, [this, metrics_callback = std::move(metrics_callback)]() {
    for (const auto& m : measurements_) {
      metrics_callback(m);
    }
    measurements_.clear();
  });
}

}  // namespace cobalt
