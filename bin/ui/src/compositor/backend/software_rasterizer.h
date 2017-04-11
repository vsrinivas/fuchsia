// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_SRC_COMPOSITOR_BACKEND_SOFTWARE_RASTERIZER_H_
#define APPS_MOZART_SRC_COMPOSITOR_BACKEND_SOFTWARE_RASTERIZER_H_

#include "apps/mozart/src/compositor/backend/framebuffer.h"
#include "apps/mozart/src/compositor/backend/framebuffer_output.h"
#include "apps/mozart/src/compositor/backend/rasterizer.h"
#include "third_party/skia/include/core/SkSurface.h"

namespace compositor {

// Rasterizer backed by a Framebuffer on a virtual console. Uses Skia CPU
// backend.
class SoftwareRasterizer : public Rasterizer {
 public:
  explicit SoftwareRasterizer(
      const RasterizeFrameFinishedCallback& frame_finished_callback);
  ~SoftwareRasterizer() override;

  void DrawFrame(ftl::RefPtr<RenderFrame> frame,
                 uint32_t frame_number,
                 ftl::TimePoint submit_time) override;

  bool Initialize(mx_display_info_t* mx_display_info) override;

 private:
  std::unique_ptr<Framebuffer> framebuffer_;
  sk_sp<SkSurface> framebuffer_surface_;
};

}  // namespace compositor

#endif  // APPS_MOZART_SRC_COMPOSITOR_BACKEND_SOFTWARE_RASTERIZER_H_
