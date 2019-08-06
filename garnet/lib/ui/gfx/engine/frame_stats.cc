// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/engine/frame_stats.h"

#include <math.h>

#include <list>
#include <string>

#include "garnet/lib/ui/gfx/util/time.h"

namespace scenic_impl {
namespace gfx {

FrameStats::FrameStats(inspect_deprecated::Node inspect_node)
    : inspect_node_(std::move(inspect_node)) {
  inspect_frame_stats_dump_ = inspect_node_.CreateLazyStringProperty("Aggregate Stats", [this] {
    std::ostringstream output;
    output << std::endl;
    ReportStats(&output);
    return output.str();
  });
}

void FrameStats::RecordFrame(FrameTimings::Timestamps timestamps,
                             zx::duration display_vsync_interval) {
  ++frame_count_;

  if (timestamps.actual_presentation_time == FrameTimings::kTimeDropped) {
    RecordDroppedFrame(timestamps);
  } else if (timestamps.actual_presentation_time - display_vsync_interval >=
             timestamps.target_presentation_time) {
    RecordDelayedFrame(timestamps);
  }

  frame_times_.push_front(timestamps);
  if (frame_times_.size() > kNumFramesToReport) {
    frame_times_.pop_back();
  }
}

void FrameStats::RecordDroppedFrame(const FrameTimings::Timestamps timestamps) {
  ++dropped_frame_count_;

  dropped_frames_.push_front(timestamps);
  if (dropped_frames_.size() > kNumDroppedFramesToReport) {
    dropped_frames_.pop_back();
  }
}

void FrameStats::RecordDelayedFrame(const FrameTimings::Timestamps timestamps) {
  ++delayed_frame_count_;

  delayed_frames_.push_front(timestamps);
  if (delayed_frames_.size() > kNumDelayedFramesToReport) {
    delayed_frames_.pop_back();
  }
}

/* static */
zx::duration FrameStats::CalculateAverageDuration(
    const std::deque<const FrameTimings::Timestamps>& timestamps,
    std::function<zx::duration(const FrameTimings::Timestamps&)> duration_func,
    uint32_t percentile) {
  FXL_DCHECK(percentile <= 100);

  const size_t num_frames = timestamps.size();
  std::list<const zx::duration> durations;
  for (auto& times : timestamps) {
    durations.emplace_back(duration_func(times));
  }
  durations.sort(std::greater<zx::duration>());

  // Time the sorted durations to only calculate the desired percentile.
  double trim_index =
      static_cast<double>(num_frames) * static_cast<double>((100 - percentile)) / 100.;
  size_t trim = ceil(trim_index);
  FXL_DCHECK(trim <= num_frames);
  for (size_t i = 0; i < trim; i++) {
    durations.pop_back();
  }

  if (durations.size() == 0u) {
    return zx::duration(0);
  }

  auto total_duration = zx::duration(0);
  for (auto& duration : durations) {
    total_duration += duration;
  }

  uint64_t num_durations = durations.size();
  return total_duration / num_durations;
}

void FrameStats::ReportStats(std::ostream* output) const {
  FXL_DCHECK(output);

  FXL_DCHECK(dropped_frame_count_ <= frame_count_);
  FXL_DCHECK(delayed_frame_count_ <= frame_count_);
  uint64_t dropped_percentage = frame_count_ > 0 ? dropped_frame_count_ * 100 / frame_count_ : 0;
  FXL_DCHECK(delayed_frame_count_ <= frame_count_);
  uint64_t delayed_percentage = frame_count_ > 0 ? delayed_frame_count_ * 100 / frame_count_ : 0;

  *output << "Total Frames: " << frame_count_ << "\n";
  *output << "Number of Dropped Frames: " << dropped_frame_count_ << " (" << dropped_percentage
          << "%)\n";
  *output << "Number of Delayed Frames (missed VSYNC): " << delayed_frame_count_ << " ("
          << delayed_percentage << "%)\n";

  auto prediction_accuracy = [](const FrameTimings::Timestamps& times) -> zx::duration {
    return times.actual_presentation_time - times.target_presentation_time;
  };
  auto total_frame_time = [](const FrameTimings::Timestamps& times) -> zx::duration {
    return times.actual_presentation_time - times.latch_point_time;
  };
  auto latency = [](const FrameTimings::Timestamps& times) -> zx::duration {
    return times.actual_presentation_time - times.render_done_time;
  };

  auto pretty_print_ms = [](zx::duration duration) -> float {
    zx::duration dur(duration);
    uint64_t usec_duration = dur.to_usecs();
    float msec = static_cast<float>(usec_duration) / 1000.f;
    return msec;
  };

  *output << "\nAverage times of the last " << kNumFramesToReport << " frames (times in ms): \n";
  *output << "Average Predication Accuracy (95 percentile): "
          << pretty_print_ms(CalculateAverageDuration(frame_times_, prediction_accuracy, 95))
          << "\n";
  *output << "Average Total Frame Time (95 percentile): "
          << pretty_print_ms(CalculateAverageDuration(frame_times_, total_frame_time, 95)) << "\n";
  *output << "Average Frame Latency (95 percentile): "
          << pretty_print_ms(CalculateAverageDuration(frame_times_, latency, 95)) << "\n";

  *output << "\nAverage times of the last " << kNumDelayedFramesToReport
          << " delayed frames (times in ms): \n";
  *output << "Average Predication Accuracy of Delayed Frames (95 percentile): "
          << pretty_print_ms(CalculateAverageDuration(delayed_frames_, prediction_accuracy, 95))
          << "\n";
  *output << "Average Total Frame Time of Delayed Frames (95 percentile): "
          << pretty_print_ms(CalculateAverageDuration(delayed_frames_, total_frame_time, 95))
          << "\n";
  *output << "Average Latency of Delayed Frames (95 percentile): "
          << pretty_print_ms(CalculateAverageDuration(delayed_frames_, prediction_accuracy, 95))
          << "\n";
}

/* static */
void FrameStats::FrameTimingsOutputToCsv(
    const std::deque<const FrameTimings::Timestamps>& timestamps, std::ostream* output) {
  for (auto& times : timestamps) {
    *output << times.latch_point_time << ",";
    *output << times.update_done_time << ",";
    *output << times.render_start_time << ",";
    *output << times.render_done_time << ",";
    *output << times.target_presentation_time << ",";
    *output << times.actual_presentation_time << "\n";
  }
}

}  // namespace gfx
}  // namespace scenic_impl
