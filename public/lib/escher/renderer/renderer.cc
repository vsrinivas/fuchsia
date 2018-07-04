// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/renderer/renderer.h"

#include <array>

#include "lib/escher/escher.h"
#include "lib/escher/impl/command_buffer_pool.h"
#include "lib/escher/impl/image_cache.h"
#include "lib/escher/impl/vulkan_utils.h"
#include "lib/escher/profiling/timestamp_profiler.h"
#include "lib/escher/scene/stage.h"
#include "lib/escher/util/stopwatch.h"
#include "lib/escher/util/trace_macros.h"
#include "lib/escher/vk/framebuffer.h"
#include "lib/escher/vk/image.h"

namespace escher {

Renderer::Renderer(EscherWeakPtr weak_escher)
    : context_(weak_escher->vulkan_context()), escher_(std::move(weak_escher)) {
  escher()->IncrementRendererCount();
}

Renderer::~Renderer() { escher()->DecrementRendererCount(); }

void Renderer::RunOffscreenBenchmark(
    uint32_t framebuffer_width, uint32_t framebuffer_height,
    vk::Format framebuffer_format, size_t frame_count,
    std::function<void(const FramePtr& frame, const ImagePtr&)> draw_func) {
  constexpr uint64_t kSecondsToNanoseconds = 1000000000;
  const char* kTraceLiteral = "RunOffscreenBenchmark";

  uint64_t frame_number = 0;
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

      auto frame = escher()->NewFrame(kTraceLiteral, ++frame_number);
      draw_func(frame, images[i]);
      frame->EndFrame(semaphores[i], nullptr);
    }

    // Prepare all semaphores to be waited-upon, and wait for the throwaway
    // frames to finish.
    auto command_buffer = escher()->command_buffer_pool()->GetCommandBuffer();
    command_buffer->Submit(context_.queue, nullptr);
    FXL_CHECK(vk::Result::eSuccess ==
              command_buffer->Wait(kSwapchainSize * kSecondsToNanoseconds));
  }

  // Render the benchmark frames.
  Stopwatch stopwatch;
  stopwatch.Start();

  impl::CommandBuffer* throttle = nullptr;
  for (size_t current_frame = 0; current_frame < frame_count; ++current_frame) {
    size_t image_index = current_frame % kSwapchainSize;

    auto command_buffer = escher()->command_buffer_pool()->GetCommandBuffer();
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

    auto frame = escher()->NewFrame(kTraceLiteral, ++frame_number,
                                    current_frame == frame_count - 1);
    draw_func(frame, images[image_index]);
    frame->EndFrame(semaphores[image_index], nullptr);
  }

  // Wait for the last frame to finish.
  auto command_buffer = escher()->command_buffer_pool()->GetCommandBuffer();
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
