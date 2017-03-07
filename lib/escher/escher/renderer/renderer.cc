// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/renderer/renderer.h"

#include "escher/impl/command_buffer_pool.h"
#include "escher/impl/escher_impl.h"
#include "escher/impl/vulkan_utils.h"
#include "escher/profiling/timestamp_profiler.h"
#include "escher/renderer/framebuffer.h"
#include "escher/renderer/image.h"

namespace escher {

Renderer::Renderer(impl::EscherImpl* escher)
    : escher_(escher),
      context_(escher_->vulkan_context()),
      pool_(escher->command_buffer_pool()) {
  escher_->IncrementRendererCount();
}

Renderer::~Renderer() {
  FTL_DCHECK(!current_frame_);
  escher_->DecrementRendererCount();
}

void Renderer::BeginFrame() {
  FTL_DCHECK(!current_frame_);
  ++frame_number_;
  current_frame_ = pool_->GetCommandBuffer();

  FTL_DCHECK(!profiler_);
  if (enable_profiling_ && escher_->supports_timer_queries()) {
    profiler_ = ftl::MakeRefCounted<TimestampProfiler>(
        context_.device, escher_->timestamp_period());
    profiler_->AddTimestamp(current_frame_,
                            vk::PipelineStageFlagBits::eTopOfPipe,
                            "start of frame");
  }
}

void Renderer::SubmitPartialFrame() {
  FTL_DCHECK(current_frame_);
  current_frame_->Submit(context_.queue, nullptr);
  current_frame_ = pool_->GetCommandBuffer();
}

void Renderer::EndFrame(const SemaphorePtr& frame_done,
                        FrameRetiredCallback frame_retired_callback) {
  FTL_DCHECK(current_frame_);
  current_frame_->AddSignalSemaphore(frame_done);
  if (profiler_) {
    // Avoid implicit reference to this in closure.
    TimestampProfilerPtr profiler = std::move(profiler_);
    auto frame_number = frame_number_;
    current_frame_->Submit(context_.queue, [frame_retired_callback, profiler,
                                            frame_number]() {
      if (frame_retired_callback) {
        frame_retired_callback();
      }
      FTL_LOG(INFO) << "------------------------------------------------------";

      FTL_LOG(INFO) << "Timestamps for frame #" << frame_number;
      FTL_LOG(INFO) << "total\t | \tsince previous (all times in microseconds)";
      FTL_LOG(INFO) << "------------------------------------------------------";
      auto timestamps = profiler->GetQueryResults();
      uint64_t previous_time = timestamps[0].elapsed;
      for (size_t i = 1; i < timestamps.size(); ++i) {
        uint64_t time = timestamps[i].elapsed;
        FTL_LOG(INFO) << time << " \t | \t" << (time - previous_time) << "   \t"
                      << timestamps[i].name;
        previous_time = time;
      }
    });
  } else {
    current_frame_->Submit(context_.queue, std::move(frame_retired_callback));
  }
  current_frame_ = nullptr;

  escher_->Cleanup();
}

void Renderer::AddTimestamp(const char* name) {
  FTL_DCHECK(current_frame_);
  if (profiler_) {
    profiler_->AddTimestamp(current_frame_,
                            vk::PipelineStageFlagBits::eBottomOfPipe, name);
  }
}

}  // namespace escher
