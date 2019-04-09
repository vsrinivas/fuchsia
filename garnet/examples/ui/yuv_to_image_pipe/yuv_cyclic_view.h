// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_UI_YUV_TO_IMAGE_PIPE_YUV_CYCLIC_VIEW_H_
#define GARNET_EXAMPLES_UI_YUV_TO_IMAGE_PIPE_YUV_CYCLIC_VIEW_H_

#include "garnet/examples/ui/yuv_to_image_pipe/yuv_base_view.h"

namespace yuv_to_image_pipe {

// Displays a YUV frame that moves around the screen in a cycle.
class YuvCyclicView : public YuvBaseView {
 public:
  YuvCyclicView(scenic::ViewContext context,
                fuchsia::images::PixelFormat pixel_format);
  ~YuvCyclicView() override = default;

 private:
  // |scenic::BaseView|
  // Called when the scene is "invalidated". Invalidation happens when surface
  // dimensions or metrics change, but not necessarily when surface contents
  // change.
  void OnSceneInvalidated(
      fuchsia::images::PresentationInfo presentation_info) override;

  FXL_DISALLOW_COPY_AND_ASSIGN(YuvCyclicView);
};

}  // namespace yuv_to_image_pipe

#endif  // GARNET_EXAMPLES_UI_YUV_TO_IMAGE_PIPE_YUV_CYCLIC_VIEW_H_
