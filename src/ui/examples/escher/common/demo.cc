// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/examples/escher/common/demo.h"

#include <lib/syslog/cpp/macros.h>

#include <array>
#include <chrono>
#include <thread>

#include "src/ui/lib/escher/impl/command_buffer_pool.h"
#include "src/ui/lib/escher/impl/image_cache.h"
#include "src/ui/lib/escher/impl/vulkan_utils.h"
#include "src/ui/lib/escher/renderer/frame.h"
#include "src/ui/lib/escher/util/stopwatch.h"
#include "src/ui/lib/escher/util/trace_macros.h"

Demo::Demo(escher::EscherWeakPtr escher, const char* name)
    : name_(name), escher_(escher), vulkan_context_(escher_->vulkan_context()) {}

Demo::~Demo() {}

bool Demo::HandleKeyPress(std::string key) {
  if (key.size() > 1) {
    if (key == "ESCAPE") {
      return false;
    } else if (key == "SPACE") {
      return false;
    } else if (key == "RETURN") {
      return false;
    }
    // Illegal value.
    FX_LOGS(ERROR) << "Cannot handle key value: " << key;
    FX_CHECK(false);
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
#ifdef __linux__
  if (tracer_) {
    tracer_.reset();
    FX_LOGS(INFO) << "Tracing disabled.";
  } else {
    tracer_ = std::make_unique<escher::Tracer>();
    FX_LOGS(INFO) << "Tracing enabled.";
  }
#else
  FX_LOGS(INFO) << "ToggleTracing() only supported for Escher-Linux.";
#endif
}

void Demo::RunOffscreenBenchmark(Demo* demo, uint32_t framebuffer_width,
                                 uint32_t framebuffer_height, vk::Format framebuffer_format,
                                 size_t frame_count) {
  constexpr uint64_t kSecondsToNanoseconds = 1000000000;
  const char* kTraceLiteral = "RunOffscreenBenchmark";

  // Clean up before running benchmark.
  auto escher = demo->escher();
  escher::ESCHER_DCHECK_VK_RESULT(escher->vk_device().waitIdle());
  escher->Cleanup();

  uint64_t frame_number = 0;

  // Create the images that we will render into, and the semaphores that will
  // prevent us from rendering into the same image concurrently.  At the same
  // time, draw a few throwaway frames, to warm things up before beginning the
  // benchmark (this also signals the semaphores so that they can be waited
  // upon in the actual benchmark run).
  constexpr size_t kSwapchainSize = 2;
  std::array<escher::SemaphorePtr, kSwapchainSize> semaphores;
  std::array<escher::ImagePtr, kSwapchainSize> images;
  {
    auto image_cache = escher->image_cache();
    for (size_t i = 0; i < kSwapchainSize; ++i) {
      auto im = image_cache->NewImage({framebuffer_format, framebuffer_width, framebuffer_height, 1,
                                       vk::ImageUsageFlagBits::eColorAttachment |
                                           vk::ImageUsageFlagBits::eTransferSrc |
                                           vk::ImageUsageFlagBits::eTransferDst,
                                       vk::MemoryPropertyFlagBits::eDeviceLocal,
                                       vk::ImageTiling::eOptimal, escher::ColorSpace::kSrgb});
      images[i] = im;
      semaphores[i] = escher::Semaphore::New(escher->vk_device());

      auto frame = escher->NewFrame(kTraceLiteral, ++frame_number);

      im->set_swapchain_layout(vk::ImageLayout::eColorAttachmentOptimal);
      frame->cmds()->ImageBarrier(im, vk::ImageLayout::eUndefined, im->swapchain_layout(),
                                  vk::PipelineStageFlagBits::eAllCommands,
                                  vk::AccessFlagBits::eColorAttachmentWrite,
                                  vk::PipelineStageFlagBits::eColorAttachmentOutput,
                                  vk::AccessFlagBits::eColorAttachmentWrite);

      demo->DrawFrame(frame, images[i], escher::SemaphorePtr());
      frame->EndFrame(semaphores[i], []() {});
    }

    // Prepare all semaphores to be waited-upon, and wait for the throwaway
    // frames to finish.
    auto result = escher->vk_device().waitIdle();
    FX_CHECK(result == vk::Result::eSuccess);
    escher->Cleanup();
  }

  // Render the benchmark frames.
  escher::Stopwatch stopwatch;
  stopwatch.Start();

  uint32_t frames_in_flight = 0;
  for (size_t current_frame = 0; current_frame < frame_count; ++current_frame) {
    FX_DCHECK(frames_in_flight <= kMaxOutstandingFrames);
    while (frames_in_flight == kMaxOutstandingFrames) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      escher->Cleanup();
    }

    // Avoid drawing multiple frames at the same time to the same image by waiting for and signaling
    // the same semaphore.  As described above, all semaphores are guaranteed to have been signaled
    // the first time they are encountered in this loop.
    size_t image_index = current_frame % kSwapchainSize;
    ++frames_in_flight;
    auto frame = escher->NewFrame(kTraceLiteral, ++frame_number, current_frame == frame_count - 1);
    demo->DrawFrame(frame, images[image_index], semaphores[image_index]);
    frame->EndFrame(semaphores[image_index], [&]() { --frames_in_flight; });

    escher->Cleanup();
  }

  // Wait for the last frame to finish.
  escher::ESCHER_CHECKED_VK_RESULT(escher->vk_device().waitIdle());
  stopwatch.Stop();
  FX_CHECK(escher->Cleanup());

  FX_LOGS(INFO) << "------------------------------------------------------";
  FX_LOGS(INFO) << "Offscreen benchmark";
  FX_LOGS(INFO) << "Rendered " << frame_count << " " << framebuffer_width << "x"
                << framebuffer_height << " frames in " << stopwatch.GetElapsedSeconds()
                << " seconds";
  FX_LOGS(INFO) << (frame_count / stopwatch.GetElapsedSeconds()) << " FPS";
  FX_LOGS(INFO) << "------------------------------------------------------";
}
