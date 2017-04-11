// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/compositor/backend/vulkan_rasterizer.h"

#include "apps/mozart/src/compositor/backend/framebuffer.h"
#include "apps/mozart/src/compositor/backend/framebuffer_output.h"
#include "apps/mozart/src/compositor/render/render_frame.h"
#include "apps/tracing/lib/trace/event.h"
#include "flutter/vulkan/vulkan_native_surface_magma.h"
#include "flutter/vulkan/vulkan_window.h"
#include "lib/ftl/logging.h"
#include "third_party/skia/include/core/SkCanvas.h"

namespace compositor {
namespace {
// Maximum amount of time to wait for a fence to clear.
constexpr ftl::TimeDelta kFenceTimeout = ftl::TimeDelta::FromMilliseconds(5000);
}

VulkanRasterizer::VulkanRasterizer(
    const RasterizeFrameFinishedCallback& callback)
    : Rasterizer(callback) {}

VulkanRasterizer::~VulkanRasterizer() {}

std::unique_ptr<vulkan::VulkanWindow> VulkanRasterizer::InitializeVulkanWindow(
    int32_t surface_width,
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

bool VulkanRasterizer::Initialize(mx_display_info_t* mx_display_info) {
  FTL_DCHECK(mx_display_info);
  TRACE_DURATION("gfx", "InitializeRasterizer");

  // TODO: Don't open the virtual console framebuffer once we have a proper
  // Fuchsia Display API.
  framebuffer_ = Framebuffer::OpenFromDisplay();
  if (!framebuffer_) {
    FTL_LOG(ERROR) << "Failed to open display";
    return false;
  }

  auto window = InitializeVulkanWindow(framebuffer_->info().width,
                                       framebuffer_->info().height);
  if (window == nullptr) {
    return false;
  }

  window_ = std::move(window);

  (*mx_display_info) = framebuffer_->info();

  return true;
}

void VulkanRasterizer::DrawFrame(ftl::RefPtr<RenderFrame> frame,
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

  frame_finished_callback_(frame_number, submit_time, start_time, finish_time);
}

}  // namespace compositor
