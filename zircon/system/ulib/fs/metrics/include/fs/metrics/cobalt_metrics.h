// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstdint>
#include <memory>
#include <utility>

#include <cobalt-client/cpp/collector.h>
#include <cobalt-client/cpp/histogram.h>
#include <fbl/string.h>

namespace fs_metrics {
// Vnode related histograms.
struct VnodeMetrics {
  // Number of buckets used for the vnode metrics.
  static constexpr uint32_t kHistogramBuckets = 10;

  VnodeMetrics(cobalt_client::Collector* collector, const fbl::String& fs_name);

  cobalt_client::Histogram<kHistogramBuckets> close;
  cobalt_client::Histogram<kHistogramBuckets> read;
  cobalt_client::Histogram<kHistogramBuckets> write;
  cobalt_client::Histogram<kHistogramBuckets> append;
  cobalt_client::Histogram<kHistogramBuckets> truncate;
  cobalt_client::Histogram<kHistogramBuckets> set_attr;
  cobalt_client::Histogram<kHistogramBuckets> get_attr;
  cobalt_client::Histogram<kHistogramBuckets> sync;
  cobalt_client::Histogram<kHistogramBuckets> read_dir;
  cobalt_client::Histogram<kHistogramBuckets> look_up;
  cobalt_client::Histogram<kHistogramBuckets> create;
  cobalt_client::Histogram<kHistogramBuckets> unlink;
  cobalt_client::Histogram<kHistogramBuckets> link;

  // Mirrors |Metrics::IsEnabled|, such that |VnodeMetrics| is self sufficient
  // to determine whether metrics should be logged or not.
  bool metrics_enabled = false;
};

// Provides a base class for collecting metrics in FS implementations. This is optional, but
// provides a source of truth of how data is collected for filesystems. Specific filesystem
// implementations with custom APIs can extend and collect more data, but for basic operations, this
// class provides the base infrastructure.
//
// TODO(gevalentino): Define the |event_code| per metric. Currently is ignored.
class Metrics {
 public:
  Metrics() = delete;
  Metrics(std::unique_ptr<cobalt_client::Collector> collector, const fbl::String& fs_name);
  Metrics(const Metrics&) = delete;
  Metrics(Metrics&&) = delete;
  Metrics& operator=(const Metrics&) = delete;
  Metrics& operator=(Metrics&&) = delete;
  virtual ~Metrics() = default;

  // Sets metric collection status to |should_collect|.
  void EnableMetrics(bool should_enable);

  // Returns true if the Logger is collecting.
  bool IsEnabled() const;

  // Returns the collector.
  const cobalt_client::Collector& collector() const { return *collector_; }

  // Returns the collector.
  cobalt_client::Collector* mutable_collector() { return collector_.get(); }

  const VnodeMetrics& vnode_metrics() const;
  VnodeMetrics* mutable_vnode_metrics();

 protected:
  std::unique_ptr<cobalt_client::Collector> collector_;

  VnodeMetrics vnode_metrics_;

  bool is_enabled_ = false;
};

}  // namespace fs_metrics
