// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_SCHEDULING_FRAME_STATS_H_
#define SRC_UI_SCENIC_LIB_SCHEDULING_FRAME_STATS_H_

#include <lib/async/cpp/task.h>
#include <lib/zx/time.h>

#include <deque>

#include "src/lib/cobalt/cpp/cobalt_logger.h"
#include "src/lib/inspect_deprecated/inspect.h"
#include "src/ui/scenic/lib/scheduling/frame_timings.h"
#include "third_party/cobalt/src/registry/buckets_config.h"

namespace scheduling {

// Class for managing and reporting frame stats from reported
// FrameTiming::Timestamps. Used for debug data, i.e. inspect.
class FrameStats {
 public:
  FrameStats(inspect_deprecated::Node inspect_node,
             std::unique_ptr<cobalt::CobaltLogger> cobalt_logger);

  void RecordFrame(FrameTimings::Timestamps timestamps, zx::duration display_vsync_interval);

  // Time interval between each flush is 10 minutes.
  static constexpr zx::duration kCobaltDataCollectionInterval = zx::min(10);

 private:
  typedef std::unordered_map<uint32_t, uint32_t> CobaltFrameHistogram;
  static constexpr size_t kNumFramesToReport = 200;
  static constexpr size_t kNumDroppedFramesToReport = 50;
  static constexpr size_t kNumDelayedFramesToReport = 50;

  // TODO(SCN-1501) Record all frame times to VMO, separate from Inspect.
  static void FrameTimingsOutputToCsv(const std::deque<const FrameTimings::Timestamps>& timestamps,
                                      std::ostream* output);

  static zx::duration CalculateAverageDuration(
      const std::deque<const FrameTimings::Timestamps>& timestamps,
      std::function<zx::duration(const FrameTimings::Timestamps&)> duration_func,
      uint32_t percentile);

  void RecordDroppedFrame(const FrameTimings::Timestamps timestamps);
  void RecordDelayedFrame(const FrameTimings::Timestamps timestamps);

  void ReportStats(std::ostream* output) const;

  // Note that both scenic_render_time and scenic_latch_to_actual_presentation
  // metrics in Cobalt has the same histogram settings. Therefore we create only
  // one bucket config for both usages.
  void InitializeFrameTimeBucketConfig();

  uint32_t GetCobaltBucketIndex(zx::duration duration);

  // Flush all histograms into Cobalt and empty them.
  void LogFrameTimes();

  // Helper function to convert histograms in unordered_map format to Cobalt-readable
  // vector of histogram buckets.
  std::vector<fuchsia::cobalt::HistogramBucket> CreateCobaltBucketsFromHistogram(
      const CobaltFrameHistogram& histogram);

  uint64_t frame_count_ = 0;
  uint64_t dropped_frame_count_ = 0;
  uint64_t delayed_frame_count_ = 0;

  // Ring buffer of the last kNum*FramesToReport.
  std::deque<const FrameTimings::Timestamps> frame_times_;
  std::deque<const FrameTimings::Timestamps> dropped_frames_;
  std::deque<const FrameTimings::Timestamps> delayed_frames_;

  inspect_deprecated::Node inspect_node_;
  inspect_deprecated::LazyStringProperty inspect_frame_stats_dump_;

  // Histograms for collecting latch point to actual presentation times.
  CobaltFrameHistogram cobalt_on_time_frame_times_histogram_;
  CobaltFrameHistogram cobalt_dropped_frame_times_histogram_;
  CobaltFrameHistogram cobalt_delayed_frame_times_histogram_;

  // Histogram for collecting render start to render done times.
  CobaltFrameHistogram cobalt_render_times_histogram_;

  // Used for getting the cobalt histogram bucket number given a frame time number.
  std::unique_ptr<cobalt::config::IntegerBucketConfig> frame_times_bucket_config_;

  std::unique_ptr<cobalt::CobaltLogger> cobalt_logger_;

  async::TaskClosureMethod<FrameStats, &FrameStats::LogFrameTimes> cobalt_logging_task_{this};
};

}  // namespace scheduling

#endif  // SRC_UI_SCENIC_LIB_SCHEDULING_FRAME_STATS_H_
