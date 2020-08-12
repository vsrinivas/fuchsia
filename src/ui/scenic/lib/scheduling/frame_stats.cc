// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/scheduling/frame_stats.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>
#include <lib/zx/resource.h>
#include <math.h>

#include <chrono>
#include <list>
#include <string>

#include "src/ui/scenic/lib/scheduling/frame_metrics_registry.cb.h"

using cobalt_registry::kScenicLatchToActualPresentationMetricId;
using cobalt_registry::kScenicRenderTimeMetricId;
using CobaltFrameStatus =
    cobalt_registry::ScenicLatchToActualPresentationMetricDimensionFrameStatus;
using cobalt_registry::kScenicRenderTimeIntBucketsFloor;
using cobalt_registry::kScenicRenderTimeIntBucketsNumBuckets;
using cobalt_registry::kScenicRenderTimeIntBucketsStepSize;

namespace scheduling {

namespace {
uint64_t TimestampsToMinuteKey(const FrameTimings::Timestamps& timestamps) {
  return timestamps.latch_point_time.get() / zx::min(1).get();
}
}  // anonymous namespace

FrameStats::FrameStats(inspect::Node inspect_node,
                       std::shared_ptr<cobalt::CobaltLogger> cobalt_logger)
    : inspect_node_(std::move(inspect_node)), cobalt_logger_(std::move(cobalt_logger)) {
  inspect_frame_stats_dump_ = inspect_node_.CreateLazyValues("Aggregate Stats", [this] {
    inspect::Inspector insp;
    ReportStats(&insp);
    return fit::make_ok_promise(std::move(insp));
  });
  InitializeFrameTimeBucketConfig();
  cobalt_logging_task_.PostDelayed(async_get_default_dispatcher(), kCobaltDataCollectionInterval);
}

void FrameStats::InitializeFrameTimeBucketConfig() {
  cobalt::IntegerBuckets bucket_proto;
  cobalt::LinearIntegerBuckets* linear = bucket_proto.mutable_linear();
  linear->set_floor(kScenicRenderTimeIntBucketsFloor);
  linear->set_num_buckets(kScenicRenderTimeIntBucketsNumBuckets);
  linear->set_step_size(kScenicRenderTimeIntBucketsStepSize);
  frame_times_bucket_config_ = cobalt::config::IntegerBucketConfig::CreateFromProto(bucket_proto);
}

void FrameStats::RecordFrame(FrameTimings::Timestamps timestamps,
                             zx::duration display_vsync_interval) {
  ++frame_count_;
  uint32_t latch_to_actual_presentation_bucket_index =
      GetCobaltBucketIndex(timestamps.actual_presentation_time - timestamps.latch_point_time);
  if (timestamps.actual_presentation_time == FrameTimings::kTimeDropped) {
    RecordDroppedFrame(timestamps);
    cobalt_dropped_frame_times_histogram_[latch_to_actual_presentation_bucket_index]++;
  } else if (timestamps.actual_presentation_time - (display_vsync_interval / 2) >=
             timestamps.target_presentation_time) {
    RecordDelayedFrame(timestamps);
    cobalt_delayed_frame_times_histogram_[latch_to_actual_presentation_bucket_index]++;
  } else {
    RecordOnTimeFrame(timestamps);
    cobalt_on_time_frame_times_histogram_[latch_to_actual_presentation_bucket_index]++;
  }
  frame_times_.push_front(timestamps);
  if (frame_times_.size() > kNumFramesToReport) {
    frame_times_.pop_back();
  }
  cobalt_render_times_histogram_[GetCobaltBucketIndex(timestamps.render_done_time -
                                                      timestamps.render_start_time)]++;
}

uint32_t FrameStats::GetCobaltBucketIndex(zx::duration duration) {
  return frame_times_bucket_config_->BucketIndex(duration.to_usecs() / 100);
}

void FrameStats::RecordDroppedFrame(const FrameTimings::Timestamps timestamps) {
  ++dropped_frame_count_;
  dropped_frames_.push_front(timestamps);
  if (dropped_frames_.size() > kNumDroppedFramesToReport) {
    dropped_frames_.pop_back();
  }
  AddHistory(HistoryStats{
      .key = TimestampsToMinuteKey(timestamps),
      .total_frames = 1,
      .dropped_frames = 1,
  });
}

void FrameStats::RecordDelayedFrame(const FrameTimings::Timestamps timestamps) {
  ++delayed_frame_count_;
  delayed_frames_.push_front(timestamps);
  if (delayed_frames_.size() > kNumDelayedFramesToReport) {
    delayed_frames_.pop_back();
  }
  AddHistory(HistoryStats{
      .key = TimestampsToMinuteKey(timestamps),
      .total_frames = 1,
      .rendered_frames = 1,
      .delayed_rendered_frames = 1,
      .render_time = timestamps.actual_presentation_time - timestamps.latch_point_time,
      .delayed_frame_render_time =
          timestamps.actual_presentation_time - timestamps.latch_point_time,
  });
}

void FrameStats::RecordOnTimeFrame(const FrameTimings::Timestamps timestamps) {
  AddHistory(HistoryStats{
      .key = TimestampsToMinuteKey(timestamps),
      .total_frames = 1,
      .rendered_frames = 1,
      .render_time = timestamps.actual_presentation_time - timestamps.latch_point_time,
  });
}

void FrameStats::AddHistory(const HistoryStats& stats) {
  // Ensure we truncated the timestamp to minutes instead of nanoseconds.
  FX_DCHECK(stats.key < 1000000000LU);

  HistoryStats* target = nullptr;
  if (!history_stats_.empty() && history_stats_.back().key == stats.key) {
    target = &history_stats_.back();
  } else {
    target = &history_stats_.emplace_back(HistoryStats{.key = stats.key});
  }

  (*target) += stats;

  while (history_stats_.size() > kNumMinutesHistory) {
    history_stats_.pop_front();
  }
}

void FrameStats::LogFrameTimes() {
  TRACE_DURATION("gfx", "FrameStats::LogFrameTimes");
  if (unlikely(cobalt_logger_ == nullptr)) {
    FX_LOGS(ERROR) << "Cobalt logger in Scenic is not initialized!";
    // Stop logging frame times into Cobalt;
    return;
  }
  if (!cobalt_on_time_frame_times_histogram_.empty()) {
    cobalt_logger_->LogIntHistogram(
        kScenicLatchToActualPresentationMetricId, CobaltFrameStatus::OnTime, "" /*component*/,
        CreateCobaltBucketsFromHistogram(cobalt_on_time_frame_times_histogram_));
    cobalt_on_time_frame_times_histogram_.clear();
  }
  if (!cobalt_dropped_frame_times_histogram_.empty()) {
    cobalt_logger_->LogIntHistogram(
        kScenicLatchToActualPresentationMetricId, CobaltFrameStatus::Dropped, "" /*component*/,
        CreateCobaltBucketsFromHistogram(cobalt_dropped_frame_times_histogram_));
    cobalt_dropped_frame_times_histogram_.clear();
  }
  if (!cobalt_delayed_frame_times_histogram_.empty()) {
    cobalt_logger_->LogIntHistogram(
        kScenicLatchToActualPresentationMetricId, CobaltFrameStatus::Delayed, "" /*component*/,
        CreateCobaltBucketsFromHistogram(cobalt_delayed_frame_times_histogram_));
    cobalt_delayed_frame_times_histogram_.clear();
  }
  if (!cobalt_render_times_histogram_.empty()) {
    cobalt_logger_->LogIntHistogram(
        kScenicRenderTimeMetricId, 0, "" /*component*/,
        CreateCobaltBucketsFromHistogram(cobalt_render_times_histogram_));
    cobalt_render_times_histogram_.clear();
  }
  cobalt_logging_task_.PostDelayed(async_get_default_dispatcher(), kCobaltDataCollectionInterval);
}

std::vector<fuchsia::cobalt::HistogramBucket> FrameStats::CreateCobaltBucketsFromHistogram(
    const CobaltFrameHistogram& histogram) {
  TRACE_DURATION("gfx", "FrameStats::CreateCobaltBucketsFromHistogram");
  std::vector<fuchsia::cobalt::HistogramBucket> buckets;
  for (const auto& pair : histogram) {
    fuchsia::cobalt::HistogramBucket bucket;
    bucket.index = pair.first;
    bucket.count = pair.second;
    buckets.push_back(std::move(bucket));
  }
  return buckets;
}

/* static */
zx::duration FrameStats::CalculateMeanDuration(
    const std::deque<const FrameTimings::Timestamps>& timestamps,
    std::function<zx::duration(const FrameTimings::Timestamps&)> duration_func,
    uint32_t percentile) {
  FX_DCHECK(percentile <= 100);

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
  FX_DCHECK(trim <= num_frames);
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

void FrameStats::ReportStats(inspect::Inspector* insp) const {
  FX_DCHECK(insp);

  FX_DCHECK(dropped_frame_count_ <= frame_count_);
  FX_DCHECK(delayed_frame_count_ <= frame_count_);
  double dropped_percentage = frame_count_ > 0 ? dropped_frame_count_ * 100.0 / frame_count_ : 0.0;
  FX_DCHECK(delayed_frame_count_ <= frame_count_);
  double delayed_percentage = frame_count_ > 0 ? delayed_frame_count_ * 100.0 / frame_count_ : 0.0;

  // Stats for the entire history.
  {
    inspect::Node node = insp->GetRoot().CreateChild("0 - Entire History");
    node.CreateUint("Total Frame Count", frame_count_, insp);
    node.CreateUint("Dropped Frame Count", dropped_frame_count_, insp);
    node.CreateDouble("Dropped Frame Percentage", dropped_percentage, insp);
    node.CreateUint("Delayed Frame Count (missed VSYNC)", delayed_frame_count_, insp);
    node.CreateDouble("Delayed Frame Percentage", delayed_percentage, insp);
    insp->emplace(std::move(node));
  }

  auto prediction_accuracy = [](const FrameTimings::Timestamps& times) -> zx::duration {
    return times.actual_presentation_time - times.target_presentation_time;
  };
  auto total_frame_time = [](const FrameTimings::Timestamps& times) -> zx::duration {
    return times.actual_presentation_time - times.latch_point_time;
  };
  auto latency = [](const FrameTimings::Timestamps& times) -> zx::duration {
    return times.actual_presentation_time - times.render_done_time;
  };

  // Stats for the last kNumFramesToReport frames.
  {
    inspect::Node node = insp->GetRoot().CreateChild("1 - Recent Frame Stats (times in ms)");

    constexpr double kUSecsToMSecs = 0.001;

    node.CreateUint("Count", frame_times_.size(), insp);

    node.CreateDouble(
        "Mean Prediction Accuracy (95 percentile)",
        kUSecsToMSecs * CalculateMeanDuration(frame_times_, prediction_accuracy, 95).to_usecs(),
        insp);

    node.CreateDouble(
        "Mean Total Frame Time (95 percentile)",
        kUSecsToMSecs * CalculateMeanDuration(frame_times_, total_frame_time, 95).to_usecs(), insp);

    node.CreateDouble("Mean Total Frame Latency (95 percentile)",
                      kUSecsToMSecs * CalculateMeanDuration(frame_times_, latency, 95).to_usecs(),
                      insp);

    insp->emplace(std::move(node));
  }

  // Stats for the last kNumFramesToReport frames.
  {
    inspect::Node node =
        insp->GetRoot().CreateChild("2 - Recent Delayed Frame Stats (times in ms)");

    node.CreateUint("Count", delayed_frames_.size(), insp);

    constexpr double kUSecsToMSecs = 0.001;

    node.CreateDouble(
        "Mean Prediction Accuracy (95 percentile)",
        kUSecsToMSecs * CalculateMeanDuration(delayed_frames_, prediction_accuracy, 95).to_usecs(),
        insp);

    node.CreateDouble(
        "Mean Total Frame Time (95 percentile)",
        kUSecsToMSecs * CalculateMeanDuration(delayed_frames_, total_frame_time, 95).to_usecs(),
        insp);

    node.CreateDouble(
        "Mean Total Frame Latency (95 percentile)",
        kUSecsToMSecs * CalculateMeanDuration(delayed_frames_, latency, 95).to_usecs(), insp);

    insp->emplace(std::move(node));
  }

  {
    inspect::Node node = insp->GetRoot().CreateChild("frame_history");

    inspect::Node minutes_ago_node = node.CreateChild("minutes_ago");
    size_t minutes_ago = 0;
    HistoryStats total = {};
    for (auto it = history_stats_.rbegin(); it != history_stats_.rend(); ++it, ++minutes_ago) {
      total += *it;
      inspect::Node cur = minutes_ago_node.CreateChild(std::to_string(minutes_ago));
      it->RecordToNode(&cur, insp);
      insp->emplace(std::move(cur));
    }

    auto total_node = node.CreateChild("total");
    total.RecordToNode(&total_node, insp);

    insp->emplace(std::move(total_node));

    insp->emplace(std::move(minutes_ago_node));
    insp->emplace(std::move(node));
  }
}

/* static */
void FrameStats::FrameTimingsOutputToCsv(
    const std::deque<const FrameTimings::Timestamps>& timestamps, std::ostream* output) {
  for (auto& times : timestamps) {
    *output << times.latch_point_time.get() << ",";
    *output << times.update_done_time.get() << ",";
    *output << times.render_start_time.get() << ",";
    *output << times.render_done_time.get() << ",";
    *output << times.target_presentation_time.get() << ",";
    *output << times.actual_presentation_time.get() << "\n";
  }
}

}  // namespace scheduling
