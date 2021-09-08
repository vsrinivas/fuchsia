// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/fshost/metrics_cobalt.h"

#include <lib/async/cpp/task.h>
#include <lib/sync/completion.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/assert.h>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <type_traits>

#include <cobalt-client/cpp/metric_options.h>

#include "src/lib/storage/vfs/cpp/metrics/cobalt_metrics.h"

namespace fshost {
namespace {
cobalt_client::MetricOptions MakeMetricOptions(fs_metrics::Event event) {
  cobalt_client::MetricOptions options;
  options.metric_id = static_cast<std::underlying_type<fs_metrics::Event>::type>(event);
  options.event_codes = {0, 0, 0, 0, 0};
  return options;
}
}  // namespace

FsHostMetricsCobalt::FsHostMetricsCobalt(std::unique_ptr<cobalt_client::Collector> collector)
    : collector_(std::move(collector)) {
  if (!collector_) {
    return;
  }
  cobalt_client::MetricOptions options = MakeMetricOptions(fs_metrics::Event::kDataCorruption);
  options.metric_dimensions = 2;
  options.event_codes[0] = static_cast<uint32_t>(fs_metrics::CorruptionSource::kMinfs);
  options.event_codes[1] = static_cast<uint32_t>(fs_metrics::CorruptionType::kMetadata);
  counters_.emplace(fs_metrics::Event::kDataCorruption,
                    std::make_unique<cobalt_client::Counter>(options, collector_.get()));
  thread_ = std::thread([this] { Run(); });
}

FsHostMetricsCobalt::~FsHostMetricsCobalt() {
  if (!thread_.joinable()) {
    return;
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    shut_down_ = true;
  }
  condition_.notify_all();
  thread_.join();
}

void FsHostMetricsCobalt::LogMinfsCorruption() {
  if (collector_) {
    counters_[fs_metrics::Event::kDataCorruption]->Increment();
  }
}

void FsHostMetricsCobalt::Flush() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    flush_ = true;
  }
  condition_.notify_all();
}

void FsHostMetricsCobalt::Run() {
  if (collector_ == nullptr) {
    return;
  }
  auto timeout_time = kSleepDuration;
  for (;;) {
    {
      std::scoped_lock<std::mutex> lock(mutex_);
      while (!flush_ && !shut_down_ &&
             condition_.wait_for(mutex_, timeout_time) != std::cv_status::timeout) {
      }
      if (shut_down_) {
        return;
      }
      flush_ = false;
    }
    if (!collector_->Flush()) {
      timeout_time = kSleepDuration;
    } else {
      // Sleep for very long time.
      timeout_time = std::chrono::hours(24 * 30);
    }
  }
}

std::unique_ptr<FsHostMetrics> DefaultMetrics() {
  return std::make_unique<FsHostMetricsCobalt>(
      std::make_unique<cobalt_client::Collector>(fs_metrics::kCobaltProjectId));
}

}  // namespace fshost
