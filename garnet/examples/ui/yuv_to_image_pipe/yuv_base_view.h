// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_UI_YUV_TO_IMAGE_PIPE_YUV_BASE_VIEW_H_
#define GARNET_EXAMPLES_UI_YUV_TO_IMAGE_PIPE_YUV_BASE_VIEW_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/ui/base_view/cpp/base_view.h>
#include <lib/ui/base_view/cpp/v1_base_view.h>
#include <lib/ui/scenic/cpp/resources.h>
#include <src/lib/fxl/logging.h>
#include <src/lib/fxl/macros.h>

namespace yuv_to_image_pipe {

// Displays a YUV frame via ImagePipe using given PixelFormat, to allow visual
// inspection that a given PixelFormat is being displayed properly by Scenic.
class YuvBaseView : public scenic::BaseView {
 public:
  YuvBaseView(scenic::ViewContext context,
              fuchsia::images::PixelFormat pixel_format);
  ~YuvBaseView() override = default;

 protected:
  // Allocates memory and adds image to ImagePipe. Returns image id.
  uint32_t AddImage();
  // Paints the given |image_id| with |pixel_multiplier|.
  void PaintImage(uint32_t image_id, uint8_t pixel_multiplier);
  // Presents given |image_id| to ImagePipe.
  void PresentImage(uint32_t image_id);

  scenic::ShapeNode node_;

 private:
  // |scenic::SessionListener|
  void OnScenicError(std::string error) override {
    FXL_LOG(ERROR) << "Scenic Error " << error;
  }

  void SetVmoPixels(uint8_t* vmo_base, uint8_t pixel_multiplier);
  void SetBgra8Pixels(uint8_t* vmo_base, uint8_t pixel_multiplier);
  void SetYuy2Pixels(uint8_t* vmo_base, uint8_t pixel_multiplier);
  void SetNv12Pixels(uint8_t* vmo_base, uint8_t pixel_multiplier);
  void SetYv12Pixels(uint8_t* vmo_base, uint8_t pixel_multiplier);

  // The return value is double so we can potentially generate nice gradients
  // for bit depths higher than 8.
  double GetYValue(double x, double y);
  double GetUValue(double x, double y);
  double GetVValue(double x, double y);

  fidl::InterfacePtr<fuchsia::images::ImagePipe> image_pipe_;
  std::map<uint32_t /* image_id */, uint8_t* /* vmo */> image_vmos_;

  fuchsia::images::PixelFormat pixel_format_ =
      fuchsia::images::PixelFormat::NV12;
  uint32_t stride_ = 0;
  uint32_t next_image_id_ = 0;

  FXL_DISALLOW_COPY_AND_ASSIGN(YuvBaseView);
};

}  // namespace yuv_to_image_pipe

#endif  // GARNET_EXAMPLES_UI_YUV_TO_IMAGE_PIPE_YUV_BASE_VIEW_H_
