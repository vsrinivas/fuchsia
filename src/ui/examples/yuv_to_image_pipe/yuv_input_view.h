// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_EXAMPLES_YUV_TO_IMAGE_PIPE_YUV_INPUT_VIEW_H_
#define SRC_UI_EXAMPLES_YUV_TO_IMAGE_PIPE_YUV_INPUT_VIEW_H_

#include "src/ui/examples/yuv_to_image_pipe/yuv_base_view.h"

namespace yuv_to_image_pipe {

// Displays a YUV frame via ImagePipe based on input.
class YuvInputView : public YuvBaseView {
 public:
  YuvInputView(scenic::ViewContext context, fuchsia::sysmem::PixelFormatType pixel_format);
  ~YuvInputView() override = default;

 private:
  void OnSceneInvalidated(fuchsia::images::PresentationInfo presentation_info) override;

  void OnInputEvent(fuchsia::ui::input::InputEvent event) override;

  uint32_t GetNextImageId();
  uint8_t GetNextPixelMultiplier();

  bool focused_ = false;
  std::vector<uint32_t> image_ids_;
  uint32_t next_image_index_ = 0;
  uint8_t pixel_multiplier_ = 255;

  FXL_DISALLOW_COPY_AND_ASSIGN(YuvInputView);
};

}  // namespace yuv_to_image_pipe

#endif  // SRC_UI_EXAMPLES_YUV_TO_IMAGE_PIPE_YUV_INPUT_VIEW_H_
