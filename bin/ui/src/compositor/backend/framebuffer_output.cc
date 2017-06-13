// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/compositor/backend/framebuffer_output.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "apps/mozart/src/compositor/backend/headless_rasterizer.h"
#include "apps/mozart/src/compositor/backend/software_rasterizer.h"
#include "apps/mozart/src/compositor/backend/vulkan_rasterizer.h"
#include "apps/mozart/src/compositor/config.h"
#include "apps/mozart/src/compositor/render/render_frame.h"
#include "apps/tracing/lib/trace/event.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/synchronization/waitable_event.h"
#include "lib/mtl/tasks/message_loop.h"
#include "lib/mtl/threading/create_thread.h"
#include "lib/mtl/threading/thread.h"

namespace compositor {
namespace {

// Delay between frames.
// TODO(jeffbrown): Don't hardcode this.
constexpr ftl::TimeDelta kHardwareRefreshInterval =
    ftl::TimeDelta::FromMicroseconds(16667);

// Amount of time it takes between flushing a frame and pixels lighting up.
// TODO(jeffbrown): Tune this for A/V sync.
constexpr ftl::TimeDelta kHardwareDisplayLatency =
    ftl::TimeDelta::FromMicroseconds(1000);

// If opening a Vulkan framebuffer fails, allow a fallback to a software
// rasterizer.
#ifdef MOZART_USE_VULKAN
constexpr bool kAllowSoftwareRasterizerFallback = true;
#endif

}  // namespace

FramebufferOutput::FramebufferOutput(Config* config)
    : compositor_task_runner_(mtl::MessageLoop::GetCurrent()->task_runner()),
      config_(config),
      weak_ptr_factory_(this) {}

FramebufferOutput::~FramebufferOutput() {
  if (rasterizer_) {
    // Safe to post "this" because we wait for this task to complete.
    rasterizer_task_runner_->PostTask([this] {
      rasterizer_.reset();
      mtl::MessageLoop::GetCurrent()->QuitNow();
    });
    rasterizer_thread_->Join();
  }
}

void FramebufferOutput::Initialize(ftl::Closure error_callback) {
  FTL_DCHECK(!rasterizer_);

  error_callback_ = error_callback;

  // Use mtl::Thread for the rasterizer because Skia's rendering code can
  // exceed the default stack size.
  rasterizer_thread_ = std::make_unique<mtl::Thread>();
  rasterizer_thread_->Run();
  rasterizer_task_runner_ = rasterizer_thread_->TaskRunner();

#ifdef MOZART_HEADLESS
  mx_display_info_t display_info = {};
  if (InitializeRasterizer(RasterizerType::kHeadless, &display_info)) {
    OnDisplayReady(display_info);
  }
#else
  // Safe to post "this" because we wait for this task to complete.
  ftl::ManualResetWaitableEvent wait;
  rasterizer_task_runner_->PostTask([this, &wait] {
#ifdef MOZART_USE_VULKAN
    WaitForDisplayDevice();
#else
    WaitForVirtualConsole();
#endif
    // |rasterizer_| is not initialized by this point. This is OK because the
    // method where |rasterizer_| is used (PostFrameToRasterizer) is not called
    // before we call OnDisplayReady, which is called after |rasterizer_| is
    // initialized.
    wait.Signal();
  });
  wait.Wait();
#endif
}

void FramebufferOutput::GetDisplayInfo(DisplayCallback callback) {
  if (display_info_) {
    callback(display_info_.Clone());
    return;
  }

  // Will resume in |OnDisplayReady|.
  display_callbacks_.push_back(std::move(callback));
}

void FramebufferOutput::ScheduleFrame(FrameCallback callback) {
  FTL_DCHECK(callback);
  FTL_DCHECK(!scheduled_frame_callback_);
  FTL_DCHECK(rasterizer_);

  scheduled_frame_callback_ = callback;

  if (!frame_in_progress_)
    RunScheduledFrameCallback();
}

void FramebufferOutput::SubmitFrame(ftl::RefPtr<RenderFrame> frame) {
  FTL_DCHECK(frame);
  FTL_DCHECK(rasterizer_);
  frame_number_++;
  TRACE_ASYNC_BEGIN("gfx", "SubmitFrame", frame_number_);

  if (frame_in_progress_) {
    if (next_frame_) {
      FTL_DLOG(WARNING) << "Discarded a frame to catch up";
      TRACE_ASYNC_END("gfx", "SubmitFrame", frame_number_ - 1);
    }
    next_frame_ = frame;
    TracePendingFrames();
    return;
  }

  frame_in_progress_ = true;
  TracePendingFrames();
  PostFrameToRasterizer(std::move(frame));
}

void FramebufferOutput::PostErrorCallback() {
  compositor_task_runner_->PostTask(error_callback_);
}

void FramebufferOutput::PostFrameToRasterizer(ftl::RefPtr<RenderFrame> frame) {
  FTL_DCHECK(frame_in_progress_);
  FTL_DCHECK(rasterizer_);

  // Safe to post "this" because this task runs on the rasterizer thread
  // which is shut down before this object is destroyed.
  rasterizer_task_runner_->PostTask(ftl::MakeCopyable([
    this, frame = std::move(frame), frame_number = frame_number_,
    submit_time = ftl::TimePoint::Now()
  ]() mutable {
    TRACE_ASYNC_BEGIN("gfx", "Rasterize", frame_number);
    rasterizer_->DrawFrame(std::move(frame), frame_number, submit_time);
  }));
}

void FramebufferOutput::DispatchDisplayReady(
    mozart::DisplayInfoPtr display_info) {
  FTL_DCHECK(display_info);
  FTL_DCHECK(!display_info_);
  FTL_DCHECK(frame_in_progress_);

  display_info_ = std::move(display_info);

  for (auto& callback : display_callbacks_)
    callback(display_info_.Clone());
}

void FramebufferOutput::OnFrameFinished(uint32_t frame_number,
                                        ftl::TimePoint submit_time,
                                        ftl::TimePoint start_time,
                                        ftl::TimePoint finish_time) {
  // TODO(jeffbrown): Tally these statistics.
  FTL_DCHECK(frame_in_progress_);

  last_presentation_time_ = finish_time + kHardwareDisplayLatency;

  // TODO(jeffbrown): Filter this feedback loop to avoid large swings.
  // presentation_latency_ = last_presentation_time_ - submit_time;
  presentation_latency_ = kHardwareRefreshInterval + kHardwareDisplayLatency;
  TRACE_ASYNC_END("gfx", "SubmitFrame", frame_number);

  PrepareNextFrame();
}

void FramebufferOutput::PrepareNextFrame() {
  FTL_DCHECK(frame_in_progress_);

  if (next_frame_) {
    PostFrameToRasterizer(std::move(next_frame_));
    TracePendingFrames();
  } else {
    frame_in_progress_ = false;
    TracePendingFrames();
    if (scheduled_frame_callback_)
      RunScheduledFrameCallback();
  }
}

void FramebufferOutput::RunScheduledFrameCallback() {
  FTL_DCHECK(scheduled_frame_callback_);
  FTL_DCHECK(!frame_in_progress_);

  FrameTiming timing;
  timing.presentation_time =
      std::max(last_presentation_time_ + kHardwareRefreshInterval,
               ftl::TimePoint::Now());
  timing.presentation_interval = kHardwareRefreshInterval;
  timing.presentation_latency = presentation_latency_;

  FrameCallback callback;
  scheduled_frame_callback_.swap(callback);
  callback(timing);
}

void FramebufferOutput::TracePendingFrames() {
  TRACE_COUNTER("gfx", "FramebufferOutput/pending",
                reinterpret_cast<uintptr_t>(this), "in_progress",
                frame_in_progress_ ? 1 : 0, "next", next_frame_ ? 1 : 0);
}

// This method is called on the rasterizer thread
bool FramebufferOutput::InitializeRasterizer(RasterizerType rasterizer_type,
                                             mx_display_info_t* display_info) {
  FTL_DCHECK(display_info);
  auto weak = weak_ptr_factory_.GetWeakPtr();
  RasterizeFrameFinishedCallback callback =
      [weak](uint32_t frame_number, ftl::TimePoint submit_time,
             ftl::TimePoint start_time, ftl::TimePoint finish_time) {
        TRACE_ASYNC_END("gfx", "Rasterize", frame_number);

        // Need a weak reference because the task may outlive this.
        weak->compositor_task_runner_->PostTask(
            [weak, frame_number, submit_time, start_time, finish_time] {
              if (weak) {
                weak->OnFrameFinished(frame_number, submit_time, start_time,
                                      finish_time);
              }
            });
      };

  std::unique_ptr<Rasterizer> rasterizer;
  switch (rasterizer_type) {
    case RasterizerType::kHeadless:
      rasterizer = std::make_unique<HeadlessRasterizer>(callback);
      break;
    case RasterizerType::kSoftware:
      rasterizer = std::make_unique<SoftwareRasterizer>(callback);
      break;
#ifdef MOZART_USE_VULKAN
    case RasterizerType::kVulkan:
      rasterizer = std::make_unique<VulkanRasterizer>(callback);
      break;
#endif
  }

  // |display_info| is set if rasterize initializes successfully
  if (rasterizer->Initialize(display_info)) {
    rasterizer_ = std::move(rasterizer);

    switch (rasterizer_type) {
      case RasterizerType::kHeadless:
        FTL_LOG(INFO) << "Mozart: Using headless rasterizer.";
        break;
      case RasterizerType::kSoftware:
        FTL_LOG(INFO) << "Mozart: Using software rasterizer.";
        break;
#ifdef MOZART_USE_VULKAN
      case RasterizerType::kVulkan:
        FTL_LOG(INFO) << "Mozart: Using Vulkan rasterizer.";
        break;
#endif
    }
    return true;
  }
  return false;
}

#ifdef MOZART_USE_VULKAN
// This method is called on the rasterizer thread
void FramebufferOutput::WaitForDisplayDevice() {
  // TODO: Replace this with the proper way of waiting for a display once
  // we have a Fuchsia Display API.
  device_watcher_ = mtl::DeviceWatcher::Create(
      "/dev/class/display", [this](int dir_fd, std::string filename) {
        FTL_LOG(INFO) << "display filename is " << filename;
        // Display name is "000" but might change in future
        device_watcher_.reset();
        DisplayDeviceReady();
      });
}

// This method is called on the rasterizer thread
void FramebufferOutput::DisplayDeviceReady() {
  // TODO: Replace this code once we have a Fuchsia Display API.
  mx_display_info_t display_info = {};
  if (InitializeRasterizer(RasterizerType::kVulkan, &display_info)) {
    OnDisplayReady(display_info);
  } else {
    if (kAllowSoftwareRasterizerFallback) {
      FTL_LOG(WARNING) << "Mozart: Failed to initialize Vulkan rasterizer, "
                          "falling back to software rasterizer.";
      WaitForVirtualConsole();
    } else {
      PostErrorCallback();
    }
  }
}
#endif

// This method is called on the rasterizer thread
void FramebufferOutput::WaitForVirtualConsole() {
  // TODO: Replace this with the proper way of waiting for a display once
  // we have a Fuchsia Display API.
  device_watcher_ = mtl::DeviceWatcher::Create(
      "/dev/class/framebuffer", [this](int dir_fd, std::string filename) {
        if (filename == "000") {
          device_watcher_.reset();
          VirtualConsoleReady();
        }
      });
}

// This method is called on the rasterizer thread
void FramebufferOutput::VirtualConsoleReady() {
  // TODO: Replace this code once we have a Fuchsia Display API.

  mx_display_info_t display_info = {};
  if (InitializeRasterizer(RasterizerType::kSoftware, &display_info)) {
    OnDisplayReady(display_info);
  } else {
    PostErrorCallback();
  }
}

// This method is called on the rasterizer thread
void FramebufferOutput::OnDisplayReady(mx_display_info_t mx_display_info) {
  // Need a weak reference because the task may outlive the output.
  auto display_info = mozart::DisplayInfo::New();
  display_info->size = mozart::Size::New();
  display_info->size->width = mx_display_info.width;
  display_info->size->height = mx_display_info.height;
  display_info->device_pixel_ratio = config_->device_pixel_ratio();
  compositor_task_runner_->PostTask(ftl::MakeCopyable([
    weak = weak_ptr_factory_.GetWeakPtr(),
    display_info = std::move(display_info)
  ]() mutable {
    if (weak) {
      weak->DispatchDisplayReady(std::move(display_info));
      weak->PrepareNextFrame();
    }
  }));
}

}  // namespace compositor
