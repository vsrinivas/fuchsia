// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_ENGINE_FRAME_STATS_H_
#define GARNET_LIB_UI_GFX_ENGINE_FRAME_STATS_H_

#include <lib/inspect/inspect.h>
#include <lib/zx/time.h>

#include <deque>

#include "garnet/lib/ui/gfx/engine/frame_timings.h"

namespace scenic_impl {
namespace gfx {
// Class for managing and reporting frame stats from reported
// FrameTiming::Timestamps. Used for debug data, i.e. inspect.
class FrameStats {
 public:
  explicit FrameStats(inspect::Node inspect_node);

  void RecordFrame(FrameTimings::Timestamps timestamps, zx_duration_t display_vsync_interval);

 private:
  static constexpr size_t kNumFramesToReport = 200;
  static constexpr size_t kNumDroppedFramesToReport = 50;
  static constexpr size_t kNumDelayedFramesToReport = 50;

  // TODO(SCN-1501) Record all frame times to VMO, separate from Inspect.
  static void FrameTimingsOutputToCsv(const std::deque<const FrameTimings::Timestamps>& timestamps,
                                      std::ostream* output);

  static zx_duration_t CalculateAverageDuration(
      const std::deque<const FrameTimings::Timestamps>& timestamps,
      std::function<zx_duration_t(const FrameTimings::Timestamps&)> duration_func,
      uint32_t percentile);

  void RecordDroppedFrame(const FrameTimings::Timestamps timestamps);
  void RecordDelayedFrame(const FrameTimings::Timestamps timestamps);

  void ReportStats(std::ostream* output) const;

  uint64_t frame_count_ = 0;
  uint64_t dropped_frame_count_ = 0;
  uint64_t delayed_frame_count_ = 0;

  // Ring buffer of the last kNum*FramesToReport.
  std::deque<const FrameTimings::Timestamps> frame_times_;
  std::deque<const FrameTimings::Timestamps> dropped_frames_;
  std::deque<const FrameTimings::Timestamps> delayed_frames_;

  inspect::Node inspect_node_;
  inspect::LazyStringProperty inspect_frame_stats_dump_;
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // GARNET_LIB_UI_GFX_ENGINE_FRAME_STATS_H_
