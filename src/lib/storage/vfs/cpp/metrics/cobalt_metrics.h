// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_STORAGE_VFS_CPP_METRICS_COBALT_METRICS_H_
#define SRC_LIB_STORAGE_VFS_CPP_METRICS_COBALT_METRICS_H_

#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>

#include <cobalt-client/cpp/collector.h>
#include <cobalt-client/cpp/counter.h>
#include <cobalt-client/cpp/histogram.h>
#include <cobalt-client/cpp/integer.h>
#include <fbl/string.h>

#include "cobalt-client/cpp/integer.h"
#include "src/lib/fxl/synchronization/thread_annotations.h"
#include "src/lib/storage/vfs/cpp/metrics/events.h"

namespace fs_metrics {

// local_storage project ID as defined in cobalt-analytics projects.yaml.
static constexpr uint32_t kCobaltProjectId = 3676913920;

// Fs related histograms.
struct FsCommonMetrics {
  // Number of buckets used for the these metrics.
  static constexpr uint32_t kHistogramBuckets = 10;

  FsCommonMetrics(cobalt_client::Collector* collector, Source source);

  struct VnodeMetrics {
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
  } vnode;

  struct JournalMetrics {
    cobalt_client::Histogram<kHistogramBuckets> write_data;
    cobalt_client::Histogram<kHistogramBuckets> write_metadata;
    cobalt_client::Histogram<kHistogramBuckets> trim_data;
    cobalt_client::Histogram<kHistogramBuckets> sync;
    cobalt_client::Histogram<kHistogramBuckets> schedule_task;
    cobalt_client::Histogram<kHistogramBuckets> writer_write_data;
    cobalt_client::Histogram<kHistogramBuckets> writer_write_metadata;
    cobalt_client::Histogram<kHistogramBuckets> writer_trim_data;
    cobalt_client::Histogram<kHistogramBuckets> writer_sync;
    cobalt_client::Histogram<kHistogramBuckets> writer_write_info_block;
  } journal;

  // Mirrors |Metrics::IsEnabled|, such that |FsCommonMetrics| is self sufficient
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
  Metrics(std::unique_ptr<cobalt_client::Collector> collector, Source source);
  Metrics(const Metrics&) = delete;
  Metrics(Metrics&&) = delete;
  Metrics& operator=(const Metrics&) = delete;
  Metrics& operator=(Metrics&&) = delete;
  virtual ~Metrics() = default;

  // Sets metric collection status to |should_collect|.
  void EnableMetrics(bool should_enable);

  // Returns true if the Logger is collecting.
  bool IsEnabled() const;

  // Flushes all metrics.  Returns true if successful.
  bool Flush();

  const FsCommonMetrics& fs_common_metrics() const;
  FsCommonMetrics* mutable_fs_common_metrics();

 private:
  struct CompareCounters {
    using is_transparent = cobalt_client::MetricOptions;

    bool operator()(const std::unique_ptr<cobalt_client::Counter>& left,
                    const std::unique_ptr<cobalt_client::Counter>& right) const {
      return cobalt_client::MetricOptions::LessThan()(left->GetOptions(), right->GetOptions());
    }

    bool operator()(const std::unique_ptr<cobalt_client::Counter>& left,
                    const cobalt_client::MetricOptions& right) const {
      return cobalt_client::MetricOptions::LessThan()(left->GetOptions(), right);
    }

    bool operator()(const cobalt_client::MetricOptions& left,
                    const std::unique_ptr<cobalt_client::Counter>& right) const {
      return cobalt_client::MetricOptions::LessThan()(left, right->GetOptions());
    }
  };

  std::mutex mutex_;
  std::unique_ptr<cobalt_client::Collector> collector_ FXL_GUARDED_BY(mutex_);

  FsCommonMetrics fs_common_metrics_;

  // Low frequency counters created on the fly with dynamic metric options.  Currently used
  // just for recording the oldest versions and discarded after flushing.
  std::set<std::unique_ptr<cobalt_client::Counter>, CompareCounters> temporary_counters_
      FXL_GUARDED_BY(mutex_);

  bool is_enabled_ = false;
};

}  // namespace fs_metrics

#endif  // SRC_LIB_STORAGE_VFS_CPP_METRICS_COBALT_METRICS_H_
