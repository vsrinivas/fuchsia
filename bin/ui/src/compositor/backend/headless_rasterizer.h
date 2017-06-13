// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_SRC_COMPOSITOR_BACKEND_HEADLESS_RASTERIZER_H_
#define APPS_MOZART_SRC_COMPOSITOR_BACKEND_HEADLESS_RASTERIZER_H_

#include "apps/mozart/src/compositor/backend/rasterizer.h"

namespace compositor {

// Rasterizer that doesn't render anything to the screen.
class HeadlessRasterizer : public Rasterizer {
 public:
  explicit HeadlessRasterizer(
      const RasterizeFrameFinishedCallback& frame_finished_callback);
  ~HeadlessRasterizer() override;

  void DrawFrame(ftl::RefPtr<RenderFrame> frame,
                 uint32_t frame_number,
                 ftl::TimePoint submit_time) override;

  bool Initialize(mx_display_info_t* mx_display_info) override;
};

}  // namespace compositor

#endif  // APPS_MOZART_SRC_COMPOSITOR_BACKEND_HEADLESS_RASTERIZER_H_
