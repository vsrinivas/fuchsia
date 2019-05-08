// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/profiling/timestamp_profiler.h"

#include "src/ui/lib/escher/impl/command_buffer.h"
#include "src/ui/lib/escher/impl/vulkan_utils.h"
#include "src/ui/lib/escher/util/trace_macros.h"

#ifdef OS_FUCHSIA
#include <zircon/syscalls.h>
#endif

namespace escher {

static constexpr uint32_t kPoolSize = 100;

TimestampProfiler::TimestampProfiler(vk::Device device, float timestamp_period)
    : device_(device), timestamp_period_(timestamp_period) {}

TimestampProfiler::~TimestampProfiler() {
  FXL_DCHECK(ranges_.empty() && pools_.empty() && query_count_ == 0 &&
             current_pool_index_ == 0);
}

void TimestampProfiler::AddTimestamp(impl::CommandBuffer* cmd_buf,
                                     vk::PipelineStageFlagBits flags,
                                     const char* name) {
  QueryRange* range = ObtainRange(cmd_buf);
  cmd_buf->vk().writeTimestamp(flags, range->pool, current_pool_index_);
  results_.push_back(Result{0, 0, 0, name});
  ++range->count;
  ++current_pool_index_;
  ++query_count_;
}

std::vector<TimestampProfiler::Result> TimestampProfiler::GetQueryResults() {
  uint32_t result_index = 0;
  for (auto& range : ranges_) {
    // We don't wait for results.  Crash if results aren't immediately
    // available.
    vk::Result status = device_.getQueryPoolResults(
        range.pool, range.start_index, range.count,
        vk::ArrayProxy<Result>(range.count, &results_[result_index]),
        sizeof(Result), vk::QueryResultFlagBits::e64);
    FXL_DCHECK(status == vk::Result::eSuccess);

    result_index += range.count;
  }
  FXL_CHECK(result_index == query_count_);

  for (auto& pool : pools_) {
    device_.destroyQueryPool(pool);
  }
  ranges_.clear();
  pools_.clear();
  query_count_ = 0;
  current_pool_index_ = 0;

  // |timestamp_period_| is the number of nanoseconds per time unit reported by
  // the timer query, so this is the number of microseconds in the same time
  // per the same time unit.  We need to use this below because an IEEE double
  // doesn't have enough precision to hold nanoseconds since the epoch.
  const double microsecond_multiplier = timestamp_period_ * 0.001;
  for (size_t i = 0; i < results_.size(); ++i) {
    // Avoid precision issues that we would have if we simply multiplied by
    // timestamp_period_.
    results_[i].raw_nanoseconds =
        1000 * static_cast<uint64_t>(results_[i].raw_nanoseconds *
                                     microsecond_multiplier);

    // Microseconds since the beginning of this timing query.
    results_[i].time =
        (results_[i].raw_nanoseconds - results_[0].raw_nanoseconds) / 1000;

    // Microseconds since the previous event.
    results_[i].elapsed = i == 0 ? 0 : results_[i].time - results_[i - 1].time;
  }

  return std::move(results_);
}

#ifdef OS_FUCHSIA
// TODO (ES-174) precision issues here?
static inline uint64_t NanosToTicks(zx_time_t nanoseconds, zx_time_t now_nanos,
                                    uint64_t now_ticks,
                                    double ticks_per_nanosecond) {
  double now_ticks_from_nanos = now_nanos * ticks_per_nanosecond;

  // Number of ticks to add to now_ticks_from_nanos to get to now_ticks.
  double offset = now_ticks - now_ticks_from_nanos;

  return static_cast<uint64_t>(nanoseconds * ticks_per_nanosecond + offset);
}

// ProcessTraceEvents transforms the data received from GetQueryResults into a
// format more suitable for tracing and logging, though it does not do any
// tracing or logging on its own.
//
// The |timestamps| vector holds an ordered sequence of timestamps. The first
// and last timestamps represent the beginning and end of the frame. All other
// timestamps were added by the application.
//
// Each of those Vulkan timestamps represents when that GPU work ended. It is
// possible for multiple timestamps to have the same value due to the specifics
// of the Vulkan implementation. If this occurs, we interpret those GPU events
// as occurring during the same period of time, and output a single trace
// event struct accordingly.
std::vector<TimestampProfiler::TraceEvent>
TimestampProfiler::ProcessTraceEvents(
    const std::vector<TimestampProfiler::Result>& timestamps) {
  std::vector<TimestampProfiler::TraceEvent> traces;

  // We need at least two timestamps to create a TraceEvent with positive
  // duration.
  if (timestamps.size() < 2)
    return traces;

  static const double kTicksPerNanosecond =
      zx_ticks_per_second() / 1000000000.0;
  const zx_time_t now_nanos = zx_clock_get_monotonic();
  const uint64_t now_ticks = zx_ticks_get();

  // start_ticks is the starting tick value for our current event.
  uint64_t start_ticks = NanosToTicks(timestamps[0].raw_nanoseconds, now_nanos,
                                      now_ticks, kTicksPerNanosecond);
  // end_ticks is the ending tick value for our current event.
  uint64_t end_ticks = NanosToTicks(timestamps[1].raw_nanoseconds, now_nanos,
                                    now_ticks, kTicksPerNanosecond);

  // Create the first trace event.
  std::vector<const char*> name = {timestamps[1].name};
  traces.push_back({start_ticks, end_ticks, name});

  for (size_t i = 2; i < timestamps.size() - 1; i++) {
    uint64_t ticks = NanosToTicks(timestamps[i].raw_nanoseconds, now_nanos,
                                  now_ticks, kTicksPerNanosecond);
    // If the ticks we see is greater than our last one, we start a new
    // TraceEvent and update our start and end tick values.
    if (ticks > end_ticks) {
      start_ticks = end_ticks;
      end_ticks = ticks;

      std::vector<const char*> names;
      names.push_back(timestamps[i].name);
      traces.push_back({start_ticks, end_ticks, names});
    } else {
      // Otherwise, we are seeing a concurrent event and should simply append
      // to the latest names vector.
      traces.back().names.push_back(timestamps[i].name);
    }
  }

  return traces;
}

// This function outputs trace events generated by the application. It is
// intended to be used in conjunction with the ProcessTraceEvents() method.
//
// We utilize virtual duration events to represent this GPU work on a virtual
// thread (vthread) since it is not local to any CPU thread.
void TimestampProfiler::TraceGpuQueryResults(
    const std::vector<TimestampProfiler::TraceEvent>& trace_events,
    uint64_t frame_number, uint64_t escher_frame_number,
    const char* trace_literal, const char* gpu_vthread_literal,
    uint64_t gpu_vthread_id) {
  constexpr static const char* kCategoryLiteral = "gfx";

  // First, create the entire duration event. We can do this by creating an
  // event combining the start of the first event, and the end of the last
  // event.
  uint64_t frame_start_ticks = trace_events[0].start_ticks;
  uint64_t frame_end_ticks = trace_events.back().end_ticks;

  TRACE_VTHREAD_DURATION_BEGIN(kCategoryLiteral, trace_literal,
                               gpu_vthread_literal, gpu_vthread_id,
                               frame_start_ticks, "Frame number", frame_number,
                               "Escher frame number", escher_frame_number);
  TRACE_VTHREAD_DURATION_END(kCategoryLiteral, trace_literal,
                             gpu_vthread_literal, gpu_vthread_id,
                             frame_end_ticks, "Frame number", frame_number,
                             "Escher frame number", escher_frame_number);

  // Now, output the more interesting events added by the application.
  for (size_t i = 0; i < trace_events.size(); i++) {
    size_t num_concurrent_events = trace_events[i].names.size();

    switch (num_concurrent_events) {
      case 1: {
        TRACE_VTHREAD_DURATION_BEGIN(kCategoryLiteral, trace_events[i].names[0],
                                     gpu_vthread_literal, gpu_vthread_id,
                                     trace_events[i].start_ticks);

        TRACE_VTHREAD_DURATION_END(kCategoryLiteral, trace_events[i].names[0],
                                   gpu_vthread_literal, gpu_vthread_id,
                                   trace_events[i].end_ticks);
        break;
      }
      case 2: {
        TRACE_VTHREAD_DURATION_BEGIN(kCategoryLiteral, trace_events[i].names[0],
                                     gpu_vthread_literal, gpu_vthread_id,
                                     trace_events[i].start_ticks,
                                     "Second Event", trace_events[i].names[1]);

        TRACE_VTHREAD_DURATION_END(kCategoryLiteral, trace_events[i].names[0],
                                   gpu_vthread_literal, gpu_vthread_id,
                                   trace_events[i].end_ticks, "Second Event",
                                   trace_events[i].names[1]);
        break;
      }
      default: {
        FXL_LOG(WARNING)
            << "We have more than 2 concurrent Vulkan timestamp events, \
                                   dropping some traces. "
            << "Have: " << num_concurrent_events << ").";
      }
    }
  }
}
#else
void TimestampProfiler::TraceGpuQueryResults(
    const std::vector<TimestampProfiler::TraceEvent>& trace_events,
    uint64_t frame_number, uint64_t escher_frame_number,
    const char* trace_literal, const char* gpu_vthread_literal,
    uint64_t gpu_vthread_id) {}

std::vector<TimestampProfiler::TraceEvent>
TimestampProfiler::ProcessTraceEvents(
    const std::vector<TimestampProfiler::Result>& timestamps) {
  std::vector<TimestampProfiler::TraceEvent> t;
  return t;
}
#endif

void TimestampProfiler::LogGpuQueryResults(
    uint64_t escher_frame_number,
    const std::vector<TimestampProfiler::Result>& timestamps) {
  FXL_LOG(INFO) << "--------------------------------"
                   "----------------------";
  FXL_LOG(INFO) << "Timestamps for frame #" << escher_frame_number;
  FXL_LOG(INFO) << "total\t | \tsince previous (all "
                   "times in microseconds)";
  FXL_LOG(INFO) << "--------------------------------"
                   "----------------------";
  for (size_t i = 0; i < timestamps.size(); ++i) {
    FXL_LOG(INFO) << timestamps[i].time << " \t | \t" << timestamps[i].elapsed
                  << "   \t" << timestamps[i].name;
  }
  FXL_LOG(INFO) << "--------------------------------"
                   "----------------------";
}

TimestampProfiler::QueryRange* TimestampProfiler::ObtainRange(
    impl::CommandBuffer* cmd_buf) {
  if (ranges_.empty() || current_pool_index_ == kPoolSize) {
    return CreateRangeAndPool(cmd_buf);
  } else if (ranges_.back().command_buffer != cmd_buf->vk()) {
    return CreateRange(cmd_buf);
  } else {
    auto range = &ranges_.back();
    FXL_DCHECK(current_pool_index_ < kPoolSize);
    if (current_pool_index_ != range->start_index + range->count) {
      FXL_LOG(INFO) << current_pool_index_ << "  " << range->start_index << "  "
                    << range->count;
    }
    FXL_DCHECK(current_pool_index_ == range->start_index + range->count);
    return range;
  }
}

TimestampProfiler::QueryRange* TimestampProfiler::CreateRangeAndPool(
    impl::CommandBuffer* cmd_buf) {
  vk::QueryPoolCreateInfo info;
  info.flags = vk::QueryPoolCreateFlags();  // no flags currently exist
  info.queryType = vk::QueryType::eTimestamp;
  info.queryCount = kPoolSize;
  vk::QueryPool pool = ESCHER_CHECKED_VK_RESULT(device_.createQueryPool(info));

  QueryRange range;
  range.pool = pool;
  range.command_buffer = cmd_buf->vk();
  range.start_index = 0;
  range.count = 0;

  current_pool_index_ = 0;
  pools_.push_back(pool);
  ranges_.push_back(range);

  return &ranges_.back();
}

TimestampProfiler::QueryRange* TimestampProfiler::CreateRange(
    impl::CommandBuffer* cmd_buf) {
  QueryRange& prev = ranges_.back();
  FXL_DCHECK(!ranges_.empty() && current_pool_index_ < kPoolSize);
  FXL_DCHECK(current_pool_index_ == prev.start_index + prev.count);

  QueryRange range;
  range.pool = prev.pool;
  range.command_buffer = cmd_buf->vk();
  range.start_index = prev.start_index + prev.count;
  range.count = 0;
  FXL_DCHECK(range.start_index == current_pool_index_);

  ranges_.push_back(range);
  return &ranges_.back();
}

}  // namespace escher
