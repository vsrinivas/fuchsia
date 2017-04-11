// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/compositor/backend/software_rasterizer.h"

#include "apps/mozart/lib/skia/skia_vmo_surface.h"
#include "apps/mozart/src/compositor/backend/framebuffer.h"
#include "apps/mozart/src/compositor/render/render_frame.h"
#include "apps/tracing/lib/trace/event.h"
#include "lib/ftl/logging.h"
#include "lib/mtl/tasks/message_loop.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkSurface.h"

namespace compositor {
namespace {
// Maximum amount of time to wait for a fence to clear.
constexpr ftl::TimeDelta kFenceTimeout = ftl::TimeDelta::FromMilliseconds(5000);
}

SoftwareRasterizer::SoftwareRasterizer(
    const RasterizeFrameFinishedCallback& callback)
    : Rasterizer(callback) {}

SoftwareRasterizer::~SoftwareRasterizer() {}

bool SoftwareRasterizer::Initialize(mx_display_info_t* mx_display_info) {
  FTL_DCHECK(mx_display_info);
  TRACE_DURATION("gfx", "InitializeRasterizer");

  framebuffer_ = Framebuffer::OpenFromVirtualConsole();
  if (!framebuffer_) {
    FTL_LOG(ERROR) << "Failed to open framebuffer.";
    return false;
  }

  SkColorType sk_color_type;
  switch (framebuffer_->info().format) {
    case MX_PIXEL_FORMAT_ARGB_8888:
    case MX_PIXEL_FORMAT_RGB_x888:
      sk_color_type = kBGRA_8888_SkColorType;
      break;
    case MX_PIXEL_FORMAT_RGB_565:
      sk_color_type = kRGB_565_SkColorType;
      break;
    default:
      FTL_LOG(ERROR) << "Framebuffer has unsupported pixel format: "
                     << framebuffer_->info().format;
      return false;
  }

  framebuffer_surface_ = mozart::MakeSkSurfaceFromVMO(
      SkImageInfo::Make(framebuffer_->info().width, framebuffer_->info().height,
                        sk_color_type, kOpaque_SkAlphaType),
      framebuffer_->info().stride * framebuffer_->info().pixelsize,
      framebuffer_->vmo());
  if (!framebuffer_surface_) {
    FTL_LOG(ERROR) << "Failed to map framebuffer surface.";
    return false;
  }

  (*mx_display_info) = framebuffer_->info();
  return true;
}

void SoftwareRasterizer::DrawFrame(ftl::RefPtr<RenderFrame> frame,
                                   uint32_t frame_number,
                                   ftl::TimePoint submit_time) {
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
    SkCanvas* canvas = framebuffer_surface_->getCanvas();
    frame->Draw(canvas);
    canvas->flush();
  }

  {
    TRACE_DURATION("gfx", "Flush");
    framebuffer_->Flush();
  }

  ftl::TimePoint finish_time = ftl::TimePoint::Now();

  frame_finished_callback_(frame_number, submit_time, start_time, finish_time);
}

}  // namespace compositor
