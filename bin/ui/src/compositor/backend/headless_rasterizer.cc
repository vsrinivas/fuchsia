// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/compositor/backend/headless_rasterizer.h"

#include <unistd.h>

#include "apps/mozart/src/compositor/render/render_frame.h"
#include "apps/tracing/lib/trace/event.h"
#include "lib/ftl/logging.h"

namespace compositor {
namespace {
// Maximum amount of time to wait for a fence to clear.
constexpr ftl::TimeDelta kFenceTimeout = ftl::TimeDelta::FromMilliseconds(5000);
}  // namespace

HeadlessRasterizer::HeadlessRasterizer(
    const RasterizeFrameFinishedCallback& callback)
    : Rasterizer(callback) {}

HeadlessRasterizer::~HeadlessRasterizer() {}

bool HeadlessRasterizer::Initialize(mx_display_info_t* mx_display_info) {
  FTL_DCHECK(mx_display_info);
  TRACE_DURATION("gfx", "InitializeRasterizer");

  // Pass dummy display information.
  mx_display_info_t display_info = {.format = 0,
                                    .width = 2180,
                                    .height = 1440,
                                    .stride = 2180,
                                    .pixelsize = 1,
                                    .flags = 0};

  (*mx_display_info) = display_info;
  return true;
}

void HeadlessRasterizer::DrawFrame(ftl::RefPtr<RenderFrame> frame,
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

  // Skip drawing
  usleep(2000);

  ftl::TimePoint finish_time = ftl::TimePoint::Now();

  frame_finished_callback_(frame_number, submit_time, start_time, finish_time);
}

}  // namespace compositor
