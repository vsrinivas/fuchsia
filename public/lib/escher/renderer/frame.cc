// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/renderer/frame.h"

#ifdef OS_FUCHSIA
#include <zircon/syscalls.h>
#endif

#include "lib/escher/escher.h"
#include "lib/escher/impl/frame_manager.h"
#include "lib/escher/util/trace_macros.h"
#include "lib/escher/vk/command_buffer.h"
#include "lib/fxl/macros.h"

namespace escher {

namespace {

// Generates a unique frame count for each created frame.
static uint64_t NextFrameNumber() {
  static std::atomic<uint64_t> counter(0);
  return ++counter;
}

}  // anonymous namespace

const ResourceTypeInfo Frame::kTypeInfo("Frame", ResourceType::kResource,
                                        ResourceType::kFrame);

Frame::Frame(impl::FrameManager* manager, BlockAllocator allocator,
             uint64_t frame_number, const char* trace_literal,
             bool enable_gpu_logging)
    : Resource(manager),
      frame_number_(frame_number),
      escher_frame_number_(NextFrameNumber()),
      trace_literal_(trace_literal),
      enable_gpu_logging_(enable_gpu_logging),
      queue_(escher()->device()->vk_main_queue()),
      block_allocator_(std::move(allocator)),
      profiler_(escher()->supports_timer_queries()
                    ? fxl::MakeRefCounted<TimestampProfiler>(
                          escher()->vk_device(), escher()->timestamp_period())
                    : TimestampProfilerPtr()) {
  FXL_DCHECK(queue_);
}

Frame::~Frame() {
  FXL_DCHECK(!command_buffer_) << "EndFrame() was not called.";
}

void Frame::BeginFrame() {
  TRACE_DURATION("gfx", "escher::Frame::BeginFrame", "frame_number",
                 frame_number_, "escher_frame_number", escher_frame_number_);
  FXL_DCHECK(!command_buffer_);

  static_cast<impl::FrameManager*>(owner())->IncrementNumOutstandingFrames();
  new_command_buffer_ = CommandBuffer::NewForGraphics(escher());
  command_buffer_ = new_command_buffer_->impl();
  vk_command_buffer_ = command_buffer_->vk();
  AddTimestamp("start of frame");
}

void Frame::SubmitPartialFrame(const SemaphorePtr& frame_done) {
  ++submission_count_;
  TRACE_DURATION("gfx", "escher::Frame::SubmitPartialFrame", "frame_number",
                 frame_number_, "submission_index", submission_count_);
  FXL_DCHECK(command_buffer_);
  command_buffer_->AddSignalSemaphore(frame_done);
  command_buffer_->Submit(queue_, nullptr);
  new_command_buffer_ = CommandBuffer::NewForGraphics(escher());
  command_buffer_ = new_command_buffer_->impl();
  vk_command_buffer_ = command_buffer_->vk();
}

void Frame::EndFrame(const SemaphorePtr& frame_done,
                     FrameRetiredCallback frame_retired_callback) {
  ++submission_count_;
  TRACE_DURATION("gfx", "escher::Frame::EndFrame", "frame_number",
                 frame_number_, "submission_index", submission_count_);

  FXL_DCHECK(command_buffer_);
  AddTimestamp("end of frame");

  command_buffer_->AddSignalSemaphore(frame_done);
  command_buffer_->Submit(
      queue_, [frame_retired_callback, profiler{std::move(profiler_)},
               frame_number = frame_number_, trace_literal = trace_literal_,
               enable_gpu_logging = enable_gpu_logging_,
               this_frame = FramePtr(this)]() {
        if (frame_retired_callback) {
          frame_retired_callback();
        }

        if (profiler) {
          auto timestamps = profiler->GetQueryResults();
          TraceGpuQueryResults(frame_number, timestamps, trace_literal);
          if (enable_gpu_logging) {
            LogGpuQueryResults(frame_number, timestamps);
          }
        }

        static_cast<impl::FrameManager*>(this_frame->owner())
            ->DecrementNumOutstandingFrames();
      });

  new_command_buffer_ = nullptr;
  command_buffer_ = nullptr;
  vk_command_buffer_ = vk::CommandBuffer();
  escher()->Cleanup();
}

void Frame::AddTimestamp(const char* name, vk::PipelineStageFlagBits stages) {
  if (profiler_)
    profiler_->AddTimestamp(command_buffer_, stages, name);
}

void Frame::LogGpuQueryResults(
    uint64_t frame_number,
    const std::vector<TimestampProfiler::Result>& timestamps) {
  FXL_LOG(INFO) << "--------------------------------"
                   "----------------------";
  FXL_LOG(INFO) << "Timestamps for frame #" << frame_number;
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

#ifndef OS_FUCHSIA
void Frame::TraceGpuQueryResults(
    uint64_t frame_number,
    const std::vector<TimestampProfiler::Result>& timestamps,
    const char* trace_literal) {}
#else
// TODO: precision issues here?
static inline uint64_t NanosToTicks(zx_time_t nanoseconds, zx_time_t now_nanos,
                                    uint64_t now_ticks,
                                    double ticks_per_nanosecond) {
  double now_ticks_from_nanos = now_nanos * ticks_per_nanosecond;

  // Number of ticks to add to now_ticks_from_nanos to get to now_ticks.
  double offset = now_ticks - now_ticks_from_nanos;

  return static_cast<uint64_t>(nanoseconds * ticks_per_nanosecond + offset);
}

void Frame::TraceGpuQueryResults(
    uint64_t frame_number,
    const std::vector<TimestampProfiler::Result>& timestamps,
    const char* trace_literal) {
  constexpr static const char* kCategoryLiteral = "gfx";
  const trace_async_id_t async_id = TRACE_NONCE();

  zx_time_t now_nanos = zx_clock_get(ZX_CLOCK_MONOTONIC);
  uint64_t now_ticks = zx_ticks_get();
  double ticks_per_nanosecond = zx_ticks_per_second() / 1000000000.0;

  trace_string_ref_t TRACE_INTERNAL_CATEGORY_REF;
  trace_context_t* TRACE_INTERNAL_CONTEXT = trace_acquire_context_for_category(
      kCategoryLiteral, &TRACE_INTERNAL_CATEGORY_REF);
  // "unlikely()" is a hint to the compiler to aid branch prediction.
  if (unlikely(TRACE_INTERNAL_CONTEXT)) {
    trace_thread_ref_t thread_ref;
    trace_context_register_current_thread(TRACE_INTERNAL_CONTEXT, &thread_ref);
    trace_string_ref_t name_ref;
    trace_context_register_string_literal(TRACE_INTERNAL_CONTEXT, trace_literal,
                                          &name_ref);

    // Begin async trace.
    {
      TRACE_INTERNAL_DECLARE_ARGS("frame#", frame_number);
      uint64_t ticks = NanosToTicks(timestamps[0].raw_nanoseconds, now_nanos,
                                    now_ticks, ticks_per_nanosecond);
      trace_context_write_async_begin_event_record(
          TRACE_INTERNAL_CONTEXT, ticks, &thread_ref,
          &TRACE_INTERNAL_CATEGORY_REF, &name_ref, async_id,
          TRACE_INTERNAL_ARGS, TRACE_INTERNAL_NUM_ARGS);
    }

    // The first and last timestamps are added by Frame::BeginFrame() and
    // EndFrame(), respectively. The timestamps in this loop are the meaningful
    // ones that were added by the application.
    for (size_t i = 1; i < timestamps.size() - 1; ++i) {
      // Only trace events that took a non-zero amount of time.  Otherwise, the
      // it will not be possible select the previous event in chrome://tracing,
      // which is the one that has the useful data.
      if (timestamps[i].elapsed) {
        TRACE_INTERNAL_DECLARE_ARGS(
            "frame#", frame_number, "name", timestamps[i].name, "elapsed",
            timestamps[i].elapsed, "total", timestamps[i].time, "index", i);
        uint64_t ticks = NanosToTicks(timestamps[i].raw_nanoseconds, now_nanos,
                                      now_ticks, ticks_per_nanosecond);
        trace_context_write_async_instant_event_record(
            TRACE_INTERNAL_CONTEXT, ticks, &thread_ref,
            &TRACE_INTERNAL_CATEGORY_REF, &name_ref, async_id,
            TRACE_INTERNAL_ARGS, TRACE_INTERNAL_NUM_ARGS);
      }
    }

    // End async trace.
    {
      TRACE_INTERNAL_DECLARE_ARGS("frame#");
      size_t i = timestamps.size() - 1;
      uint64_t ticks = NanosToTicks(timestamps[i].raw_nanoseconds, now_nanos,
                                    now_ticks, ticks_per_nanosecond);
      trace_context_write_async_end_event_record(
          TRACE_INTERNAL_CONTEXT, ticks, &thread_ref,
          &TRACE_INTERNAL_CATEGORY_REF, &name_ref, async_id,
          TRACE_INTERNAL_ARGS, TRACE_INTERNAL_NUM_ARGS);
    }

    trace_release_context(TRACE_INTERNAL_CONTEXT);
  }
}
#endif

GpuAllocator* Frame::gpu_allocator() { return escher()->gpu_allocator(); }

}  // namespace escher
