// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_SRC_COMPOSITOR_BACKEND_RASTERIZER_H_
#define APPS_MOZART_SRC_COMPOSITOR_BACKEND_RASTERIZER_H_

#include <magenta/device/display.h>

#include <thread>

#include "apps/mozart/src/compositor/backend/output.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/memory/ref_counted.h"
#include "lib/ftl/memory/weak_ptr.h"
#include "lib/ftl/tasks/task_runner.h"
#include "lib/ftl/time/time_delta.h"
#include "lib/ftl/time/time_point.h"

namespace compositor {

using RasterizeFrameFinishedCallback =
    std::function<void(uint32_t frame_number,
                       ftl::TimePoint submit_time,
                       ftl::TimePoint start_time,
                       ftl::TimePoint finish_time)>;

// Generic interface for the compositor's rasterizer
class Rasterizer {
 public:
  Rasterizer(const RasterizeFrameFinishedCallback& callback)
      : frame_finished_callback_(callback) {
    FTL_DCHECK(callback);
  }

  virtual ~Rasterizer() = default;

  virtual void DrawFrame(ftl::RefPtr<RenderFrame> frame,
                         uint32_t frame_number,
                         ftl::TimePoint submit_time) = 0;

  virtual bool Initialize(mx_display_info_t* mx_display_info) = 0;

 protected:
  RasterizeFrameFinishedCallback frame_finished_callback_;
};

}  // namespace compositor

#endif  // APPS_MOZART_SRC_COMPOSITOR_BACKEND_RASTERIZER_H_
