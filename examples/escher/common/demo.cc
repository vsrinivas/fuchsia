// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "demo.h"

#include <array>
#include <chrono>
#include <thread>

#include "lib/escher/impl/command_buffer_pool.h"
#include "lib/escher/impl/image_cache.h"
#include "lib/escher/renderer/frame.h"
#include "lib/escher/util/stopwatch.h"
#include "lib/escher/util/trace_macros.h"
#include "lib/fxl/logging.h"

static constexpr size_t kOffscreenBenchmarkFrameCount = 1000;

Demo::Demo(DemoHarness* harness, const char* name)
    : harness_(harness),
      name_(name),
      vulkan_context_(harness->GetVulkanContext()),
      escher_(harness->device_queues(), harness->filesystem()),
      swapchain_helper_(harness->GetVulkanSwapchain(),
                        escher()->vulkan_context().device,
                        escher()->vulkan_context().queue) {}

Demo::~Demo() {}

bool Demo::HandleKeyPress(std::string key) {
  if (key.size() > 1) {
    if (key == "ESCAPE") {
      harness_->SetShouldQuit();
      return true;
    } else if (key == "SPACE") {
      return false;
    } else if (key == "RETURN") {
      return false;
    }
    // Illegal value.
    FXL_LOG(ERROR) << "Cannot handle key value: " << key;
    FXL_CHECK(false);
    return false;
  } else {
    char key_char = key[0];
    switch (key_char) {
      case 'T':
        ToggleTracing();
        return true;
      default:
        return false;
    }
  }
}

void Demo::ToggleTracing() {
#ifdef __fuchsia__
  // On Fuchsia, use system-wide tracing in the usual way.
  FXL_LOG(INFO) << "ToggleTracing() only supported for Escher-Linux.";
#else
  if (tracer_) {
    tracer_.reset();
    FXL_LOG(INFO) << "Tracing disabled.";
  } else {
    tracer_ = std::make_unique<escher::Tracer>();
    FXL_LOG(INFO) << "Tracing enabled.";
  }
#endif
}

// Demo throttles the number of frames in flight, rather than relying on the
// Vulkan swapchain to do it.
static bool IsAtMaxOutstandingFrames(escher::Escher* escher) {
  constexpr uint32_t kMaxOutstandingFrames = 3;
  return escher->GetNumOutstandingFrames() >= kMaxOutstandingFrames;
}

bool Demo::MaybeDrawFrame() {
  TRACE_DURATION("gfx", "escher::Demo::MaybeDrawFrame");

  if (run_offscreen_benchmark_) {
    escher()->vk_device().waitIdle();
    escher()->Cleanup();
    run_offscreen_benchmark_ = false;

    auto& swapchain = swapchain_helper_.swapchain();

    RunOffscreenBenchmark(swapchain.width, swapchain.height, swapchain.format,
                          kOffscreenBenchmarkFrameCount);

    escher()->vk_device().waitIdle();
    escher()->Cleanup();
  }

  if (IsAtMaxOutstandingFrames(escher())) {
    // Try clean up; maybe a frame is actually already finished.
    escher()->Cleanup();
    if (IsAtMaxOutstandingFrames(escher())) {
      // Still too many frames in flight.  Try again later.
      return false;
    }
  }

  {
    TRACE_DURATION("gfx", "escher::Demo::MaybeDrawFrame (drawing)");
    auto frame =
        escher()->NewFrame(name(), ++frame_count_, enable_gpu_logging_);

    swapchain_helper_.DrawFrame(
        [&, this](const escher::ImagePtr& output_image,
                  const escher::SemaphorePtr& render_finished) {
          this->DrawFrame(frame, output_image);
          frame->EndFrame(render_finished, nullptr);
        });
  }
  escher()->Cleanup();
  return true;
}

void Demo::RunOffscreenBenchmark(uint32_t framebuffer_width,
                                 uint32_t framebuffer_height,
                                 vk::Format framebuffer_format,
                                 size_t frame_count) {
  constexpr uint64_t kSecondsToNanoseconds = 1000000000;
  const char* kTraceLiteral = "RunOffscreenBenchmark";

  uint64_t frame_number = 0;
  // Create the images that we will render into, and the semaphores that will
  // prevent us from rendering into the same image concurrently.  At the same
  // time, draw a few throwaway frames, to warm things up before beginning the
  // benchmark (this also signals the semaphores so that they can be waited
  // upon in the actual benchmark run).
  constexpr size_t kSwapchainSize = 3;
  std::array<escher::SemaphorePtr, kSwapchainSize> semaphores;
  std::array<escher::ImagePtr, kSwapchainSize> images;
  {
    auto image_cache = escher()->image_cache();
    for (size_t i = 0; i < kSwapchainSize; ++i) {
      auto im = image_cache->NewImage(
          {framebuffer_format, framebuffer_width, framebuffer_height, 1,
           vk::ImageUsageFlagBits::eColorAttachment |
               vk::ImageUsageFlagBits::eTransferSrc});
      images[i] = std::move(im);
      semaphores[i] = escher::Semaphore::New(vulkan_context_.device);

      auto frame = escher()->NewFrame(kTraceLiteral, ++frame_number);
      this->DrawFrame(frame, images[i]);
      frame->EndFrame(semaphores[i], nullptr);
    }

    // Prepare all semaphores to be waited-upon, and wait for the throwaway
    // frames to finish.
    escher()->vk_device().waitIdle();
    escher()->Cleanup();
  }

  // Render the benchmark frames.
  escher::Stopwatch stopwatch;
  stopwatch.Start();

  for (size_t current_frame = 0; current_frame < frame_count; ++current_frame) {
    while (IsAtMaxOutstandingFrames(escher())) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      escher()->Cleanup();
    }

    size_t image_index = current_frame % kSwapchainSize;

    auto frame = escher()->NewFrame(kTraceLiteral, ++frame_number,
                                    current_frame == frame_count - 1);
    frame->command_buffer()->AddWaitSemaphore(
        semaphores[image_index], vk::PipelineStageFlagBits::eBottomOfPipe);
    this->DrawFrame(frame, images[image_index]);
    frame->EndFrame(semaphores[image_index], nullptr);

    escher()->Cleanup();
  }

  // Wait for the last frame to finish.
  auto command_buffer = escher()->command_buffer_pool()->GetCommandBuffer();
  command_buffer->AddWaitSemaphore(
      semaphores[(frame_count - 1) % kSwapchainSize],
      vk::PipelineStageFlagBits::eBottomOfPipe);
  command_buffer->Submit(vulkan_context_.queue, nullptr);
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
