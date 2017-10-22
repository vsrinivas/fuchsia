// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/renderer/renderer.h"

#include <array>

#include "lib/escher/escher.h"
#include "lib/escher/impl/command_buffer_pool.h"
#include "lib/escher/impl/escher_impl.h"
#include "lib/escher/impl/image_cache.h"
#include "lib/escher/impl/vulkan_utils.h"
#include "lib/escher/profiling/timestamp_profiler.h"
#include "lib/escher/scene/stage.h"
#include "lib/escher/util/stopwatch.h"
#include "lib/escher/util/trace_macros.h"
#include "lib/escher/vk/framebuffer.h"
#include "lib/escher/vk/image.h"

namespace escher {

impl::EscherImpl* Renderer::escher_impl() const {
  return escher_->impl();
}

Renderer::Renderer(Escher* escher)
    : context_(escher->vulkan_context()),
      escher_(escher),
      pool_(escher->command_buffer_pool()) {
  escher_impl()->IncrementRendererCount();
}

Renderer::~Renderer() {
  FXL_DCHECK(!current_frame_);
  escher_impl()->DecrementRendererCount();
}

void Renderer::BeginFrame() {
  TRACE_DURATION("gfx", "escher::Renderer::BeginFrame");

  FXL_DCHECK(!current_frame_);
  ++frame_number_;
  current_frame_ = pool_->GetCommandBuffer();

  FXL_DCHECK(!profiler_);
  if (enable_profiling_ && escher_impl()->supports_timer_queries()) {
    profiler_ = fxl::MakeRefCounted<TimestampProfiler>(
        context_.device, escher_impl()->timestamp_period());
    AddTimestamp("throwaway");  // Intel/Mesa workaround; see EndFrame().
    AddTimestamp("start of frame");
  }
}

void Renderer::SubmitPartialFrame() {
  TRACE_DURATION("gfx", "escher::Renderer::SubmitPartialFrame");
  FXL_DCHECK(current_frame_);
  current_frame_->Submit(context_.queue, nullptr);
  current_frame_ = pool_->GetCommandBuffer();
}

void Renderer::EndFrame(const SemaphorePtr& frame_done,
                        FrameRetiredCallback frame_retired_callback) {
  TRACE_DURATION("gfx", "escher::Renderer::EndFrame");

  FXL_DCHECK(current_frame_);
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
      FXL_LOG(INFO) << "------------------------------------------------------";
      FXL_LOG(INFO) << "Timestamps for frame #" << frame_number;
      FXL_LOG(INFO) << "total\t | \tsince previous (all times in microseconds)";
      FXL_LOG(INFO) << "------------------------------------------------------";
      auto timestamps = profiler->GetQueryResults();
      // Workaround: Intel/Mesa gives a screwed-up value for the second time.
      // So, we add a throwaway value first (see BeginFrame()), and then fix up
      // the first value that we actually print.
      timestamps[1].time = 0;
      timestamps[1].elapsed = 0;
      timestamps[2].elapsed = timestamps[2].time;
      for (size_t i = 1; i < timestamps.size(); ++i) {
        FXL_LOG(INFO) << timestamps[i].time << " \t | \t"
                      << timestamps[i].elapsed << "   \t" << timestamps[i].name;
      }
      FXL_LOG(INFO) << "------------------------------------------------------";
    });
  } else {
    current_frame_->Submit(context_.queue, std::move(frame_retired_callback));
  }
  current_frame_ = nullptr;

  escher_impl()->Cleanup();
}

void Renderer::AddTimestamp(const char* name) {
  FXL_DCHECK(current_frame_);
  if (profiler_) {
    profiler_->AddTimestamp(current_frame_,
                            vk::PipelineStageFlagBits::eBottomOfPipe, name);
  }
}

void Renderer::RunOffscreenBenchmark(
    uint32_t framebuffer_width,
    uint32_t framebuffer_height,
    vk::Format framebuffer_format,
    size_t frame_count,
    std::function<void(const ImagePtr&, const SemaphorePtr&)> draw_func) {
  constexpr uint64_t kSecondsToNanoseconds = 1000000000;

  // Create the images that we will render into, and the semaphores that will
  // prevent us from rendering into the same image concurrently.  At the same
  // time, draw a few throwaway frames, to warm things up before beginning the
  // benchmark (this also signals the semaphores so that they can be waited
  // upon in the actual benchmark run).
  constexpr size_t kSwapchainSize = 3;
  std::array<SemaphorePtr, kSwapchainSize> semaphores;
  std::array<ImagePtr, kSwapchainSize> images;
  {
    auto image_cache = escher()->image_cache();
    for (size_t i = 0; i < kSwapchainSize; ++i) {
      auto im = image_cache->NewImage(
          {framebuffer_format, framebuffer_width, framebuffer_height, 1,
           vk::ImageUsageFlagBits::eColorAttachment |
               vk::ImageUsageFlagBits::eTransferSrc});
      images[i] = std::move(im);
      semaphores[i] = Semaphore::New(context_.device);

      draw_func(images[i], semaphores[i]);
    }

    // Prepare all semaphores to be waited-upon, and wait for the throwaway
    // frames to finish.
    auto command_buffer = pool_->GetCommandBuffer();
    command_buffer->Submit(context_.queue, nullptr);
    FXL_CHECK(vk::Result::eSuccess ==
              command_buffer->Wait(kSwapchainSize * kSecondsToNanoseconds));
  }

  // Render the benchmark frames.
  Stopwatch stopwatch;
  stopwatch.Start();

  impl::CommandBuffer* throttle = nullptr;
  bool was_profiling = enable_profiling_;
  set_enable_profiling(false);
  for (size_t current_frame = 0; current_frame < frame_count; ++current_frame) {
    size_t image_index = current_frame % kSwapchainSize;

    auto command_buffer = pool_->GetCommandBuffer();
    command_buffer->AddWaitSemaphore(semaphores[image_index],
                                     vk::PipelineStageFlagBits::eBottomOfPipe);
    command_buffer->Submit(context_.queue, nullptr);

    // Don't get too many frames ahead of the GPU.  Every time we cycle through
    // all images, wait for the previous frame that was rendered to that image
    // to finish.
    if (image_index == 0) {
      if (throttle) {
        FXL_CHECK(vk::Result::eSuccess ==
                  throttle->Wait(kSwapchainSize * kSecondsToNanoseconds));
      }
      throttle = command_buffer;
    }

    set_enable_profiling(current_frame == frame_count - 1);
    draw_func(images[image_index], semaphores[image_index]);
  }
  set_enable_profiling(was_profiling);

  // Wait for the last frame to finish.
  auto command_buffer = pool_->GetCommandBuffer();
  command_buffer->AddWaitSemaphore(
      semaphores[(frame_count - 1) % kSwapchainSize],
      vk::PipelineStageFlagBits::eBottomOfPipe);
  command_buffer->Submit(context_.queue, nullptr);
  FXL_CHECK(vk::Result::eSuccess ==
            command_buffer->Wait(kSwapchainSize * kSecondsToNanoseconds));
  stopwatch.Stop();

  FXL_LOG(INFO) << "------------------------------------------------------";
  FXL_LOG(INFO) << "Offscreen benchmark";
  FXL_LOG(INFO) << "Rendered " << frame_count << " frames in "
                << stopwatch.GetElapsedSeconds() << " seconds";
  FXL_LOG(INFO) << (frame_count / stopwatch.GetElapsedSeconds()) << " FPS";
  FXL_LOG(INFO) << "------------------------------------------------------";
}

}  // namespace escher
