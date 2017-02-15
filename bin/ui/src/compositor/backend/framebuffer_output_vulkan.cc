// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/compositor/backend/framebuffer_output_vulkan.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "apps/tracing/lib/trace/event.h"
#include "apps/mozart/lib/skia/skia_vmo_surface.h"
#include "apps/mozart/src/compositor/backend/framebuffer.h"
#include "apps/mozart/src/compositor/render/render_frame.h"
#include "flutter/vulkan/vulkan_native_surface_magma.h"
#include "flutter/vulkan/vulkan_window.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/logging.h"
#include "lib/mtl/io/device_watcher.h"
#include "lib/mtl/tasks/message_loop.h"
#include "lib/mtl/threading/create_thread.h"
#include "lib/mtl/threading/thread.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkSurface.h"

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

// Maximum amount of time to wait for a fence to clear.
constexpr ftl::TimeDelta kFenceTimeout = ftl::TimeDelta::FromMilliseconds(5000);

}  // namespace

class FramebufferOutputVulkan::Rasterizer {
 public:
  explicit Rasterizer(FramebufferOutputVulkan* output);
  ~Rasterizer();

  void DrawFrame(ftl::RefPtr<RenderFrame> frame,
                 uint32_t frame_number,
                 ftl::TimePoint submit_time);

 private:
  void VirtualConsoleReady();
  bool OpenFramebuffer();

  FramebufferOutputVulkan* const output_;

  std::unique_ptr<vulkan::VulkanWindow> window_;
  std::unique_ptr<mtl::DeviceWatcher> device_watcher_;
  std::unique_ptr<Framebuffer> framebuffer_;
};

FramebufferOutputVulkan::FramebufferOutputVulkan()
    : compositor_task_runner_(mtl::MessageLoop::GetCurrent()->task_runner()),
      weak_ptr_factory_(this) {}

FramebufferOutputVulkan::~FramebufferOutputVulkan() {
  if (rasterizer_) {
    // Safe to post "this" because we wait for this task to complete.
    rasterizer_task_runner_->PostTask([this] {
      rasterizer_.reset();
      mtl::MessageLoop::GetCurrent()->QuitNow();
    });
    rasterizer_thread_->Join();
  }
}

void FramebufferOutputVulkan::Initialize(ftl::Closure error_callback) {
  FTL_DCHECK(!rasterizer_);

  error_callback_ = error_callback;

  // Use mtl::Thread for the rasterizer because Skia's rendering code can
  // exceed the default stack size.
  rasterizer_thread_ = std::make_unique<mtl::Thread>();
  rasterizer_thread_->Run();
  rasterizer_task_runner_ = rasterizer_thread_->TaskRunner();

  // Safe to post "this" because we wait for this task to complete.
  ftl::ManualResetWaitableEvent wait;
  rasterizer_task_runner_->PostTask([this, &wait] {
    rasterizer_ = std::make_unique<Rasterizer>(this);
    wait.Signal();
  });
  wait.Wait();
}

void FramebufferOutputVulkan::GetDisplayInfo(DisplayCallback callback) {
  FTL_DCHECK(rasterizer_);

  if (display_info_) {
    callback(display_info_.Clone());
    return;
  }

  // Will resume in |OnDisplayReady|.
  display_callbacks_.push_back(std::move(callback));
}

void FramebufferOutputVulkan::ScheduleFrame(FrameCallback callback) {
  FTL_DCHECK(callback);
  FTL_DCHECK(!scheduled_frame_callback_);
  FTL_DCHECK(rasterizer_);

  scheduled_frame_callback_ = callback;

  if (!frame_in_progress_)
    RunScheduledFrameCallback();
}

void FramebufferOutputVulkan::SubmitFrame(ftl::RefPtr<RenderFrame> frame) {
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

std::unique_ptr<vulkan::VulkanWindow>
FramebufferOutputVulkan::InitializeVulkanWindow(int32_t surface_width,
                                                int32_t surface_height) {
  auto proc_table = ftl::MakeRefCounted<vulkan::VulkanProcTable>();

  if (!proc_table->HasAcquiredMandatoryProcAddresses()) {
    FTL_LOG(ERROR) << "Failed to acquire Vulkan proc addresses.";
    return nullptr;
  }

  auto native_surface = std::make_unique<vulkan::VulkanNativeSurfaceMagma>(
      surface_width, surface_height);

  if (!native_surface->IsValid()) {
    FTL_LOG(ERROR) << "Native Vulkan Magma surface is not valid.";
    return nullptr;
  }

  auto window = std::make_unique<vulkan::VulkanWindow>(
      proc_table, std::move(native_surface));

  if (!window->IsValid()) {
    FTL_LOG(ERROR) << "Vulkan window is not valid.";
    return nullptr;
  }

  return window;
}

void FramebufferOutputVulkan::PostErrorCallback() {
  compositor_task_runner_->PostTask(error_callback_);
}

void FramebufferOutputVulkan::PostFrameToRasterizer(
    ftl::RefPtr<RenderFrame> frame) {
  FTL_DCHECK(frame_in_progress_);

  // Safe to post "this" because this task runs on the rasterizer thread
  // which is shut down before this object is destroyed.
  rasterizer_task_runner_->PostTask(ftl::MakeCopyable([
    this, frame = std::move(frame), frame_number = frame_number_,
    submit_time = ftl::TimePoint::Now()
  ]() mutable {
    rasterizer_->DrawFrame(std::move(frame), frame_number, submit_time);
  }));
}

void FramebufferOutputVulkan::OnDisplayReady(
    mozart::DisplayInfoPtr display_info) {
  FTL_DCHECK(display_info);
  FTL_DCHECK(!display_info_);
  FTL_DCHECK(frame_in_progress_);

  display_info_ = std::move(display_info);

  for (auto& callback : display_callbacks_)
    callback(display_info_.Clone());

  PrepareNextFrame();
}

void FramebufferOutputVulkan::OnFrameFinished(uint32_t frame_number,
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

void FramebufferOutputVulkan::PrepareNextFrame() {
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

void FramebufferOutputVulkan::RunScheduledFrameCallback() {
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

void FramebufferOutputVulkan::TracePendingFrames() {
  TRACE_COUNTER("gfx", "FramebufferOutputVulkan/pending",
                reinterpret_cast<uintptr_t>(this), "in_progress",
                frame_in_progress_ ? 1 : 0, "next", next_frame_ ? 1 : 0);
}

FramebufferOutputVulkan::Rasterizer::Rasterizer(FramebufferOutputVulkan* output)
    : output_(output) {
  FTL_DCHECK(output_);

  // TODO: Replace this with the proper way of waiting for a display once
  // we have a Fuchsia Display API.
  device_watcher_ = mtl::DeviceWatcher::Create(
      "/dev/class/console", [this](int dir_fd, std::string filename) {
        if (filename == "vc") {
          device_watcher_.reset();
          VirtualConsoleReady();
        }
      });
}

FramebufferOutputVulkan::Rasterizer::~Rasterizer() {}

void FramebufferOutputVulkan::Rasterizer::VirtualConsoleReady() {
  // TODO: Replace this code once we have a Fuchsia Display API.
  if (!OpenFramebuffer()) {
    output_->PostErrorCallback();
    return;
  }

  // Need a weak reference because the task may outlive the output.
  auto display_info = mozart::DisplayInfo::New();
  display_info->size = mozart::Size::New();
  display_info->size->width = framebuffer_->info().width;
  display_info->size->height = framebuffer_->info().height;
  display_info->device_pixel_ratio = 1.f;  // TODO: don't hardcode this
  output_->compositor_task_runner_->PostTask(ftl::MakeCopyable([
    output_weak = output_->weak_ptr_factory_.GetWeakPtr(),
    display_info = std::move(display_info)
  ]() mutable {
    if (output_weak)
      output_weak->OnDisplayReady(std::move(display_info));
  }));
}

bool FramebufferOutputVulkan::Rasterizer::OpenFramebuffer() {
  TRACE_DURATION("gfx", "InitializeRasterizer");

  // TODO: Don't open the virtual console framebuffer once we have a proper
  // Fuchsia Display API.
  framebuffer_ = Framebuffer::Open();
  if (!framebuffer_) {
    FTL_LOG(ERROR) << "Failed to open framebuffer";
    output_->PostErrorCallback();
    return false;
  }

  auto window = InitializeVulkanWindow(framebuffer_->info().width,
                                       framebuffer_->info().height);

  if (window == nullptr) {
    return false;
  }

  window_ = std::move(window);

  return true;
}

void FramebufferOutputVulkan::Rasterizer::DrawFrame(
    ftl::RefPtr<RenderFrame> frame,
    uint32_t frame_number,
    ftl::TimePoint submit_time) {
  TRACE_ASYNC_BEGIN("gfx", "Rasterize", frame_number);
  FTL_DCHECK(frame);

  ftl::TimePoint start_time = ftl::TimePoint::Now();

  {
    TRACE_DURATION("gfx", "WaitFences");
    ftl::TimePoint wait_timeout = start_time + kFenceTimeout;
    for (const auto& image : frame->images()) {
      if (image->fence() &&
          !image->fence()->WaitReady(wait_timeout - ftl::TimePoint::Now())) {
        FTL_LOG(WARNING)
            << "Waiting for fences timed out after "
            << (ftl::TimePoint::Now() - start_time).ToMilliseconds() << " ms";
        // TODO(jeffbrown): When fences time out, we're kind of stuck.
        // We have prepared a display list for a frame which includes content
        // that was incompletely rendered.  We should just skip the frame
        // (we are already way behind anyhow), track down which scenes
        // got stuck, report them as not repsponding, destroy them, then run
        // composition again and hope everything has cleared up.
        break;
      }
    }
  }

  {
    TRACE_DURATION("gfx", "Draw");
    auto framebuffer_surface = window_->AcquireSurface();
    SkCanvas* canvas = framebuffer_surface->getCanvas();
    frame->Draw(canvas);
    canvas->flush();
  }

  {
    TRACE_DURATION("gfx", "SwapBuffers");
    window_->SwapBuffers();
  }

  ftl::TimePoint finish_time = ftl::TimePoint::Now();

  // Need a weak reference because the task may outlive the output.
  output_->compositor_task_runner_->PostTask([
    output_weak = output_->weak_ptr_factory_.GetWeakPtr(), frame_number,
    submit_time, start_time, finish_time
  ] {
    TRACE_ASYNC_END("gfx", "Rasterize", frame_number);

    if (output_weak) {
      output_weak->OnFrameFinished(frame_number, submit_time, start_time,
                                   finish_time);
    }
  });
}

}  // namespace compositor
