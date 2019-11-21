// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "metrics.h"

#include <lib/async/cpp/task.h>

#include <type_traits>

#include <cobalt-client/cpp/metric-options.h>

namespace devmgr {
namespace {
cobalt_client::MetricOptions MakeMetricOptions(fs_metrics::Event event) {
  cobalt_client::MetricOptions options;
  options.metric_id = static_cast<std::underlying_type<fs_metrics::Event>::type>(event);
  options.event_codes = {0, 0, 0, 0, 0};
  return options;
}
}  // namespace

FsHostMetrics::FsHostMetrics(std::unique_ptr<cobalt_client::Collector> collector)
    : collector_(std::move(collector)) {
  counters_.emplace(fs_metrics::Event::kDataCorruption,
                    std::make_unique<cobalt_client::Counter>(
                        MakeMetricOptions(fs_metrics::Event::kDataCorruption), collector_.get()));
}

FsHostMetrics::~FsHostMetrics() {
  if (collector_ != nullptr) {
    collector_->Flush();
  }
}

void FsHostMetrics::LogMinfsCorruption() {
  counters_[fs_metrics::Event::kDataCorruption]->Increment();
}

void FsHostMetrics::FlushUntilSuccess(async_dispatcher_t* dispatcher) {
  if (!collector_->Flush()) {
    async::PostDelayedTask(
        dispatcher, [this, dispatcher]() { FlushUntilSuccess(dispatcher); }, zx::sec(10));
  }
}

}  // namespace devmgr
