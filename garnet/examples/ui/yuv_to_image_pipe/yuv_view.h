// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_UI_YUV_TO_IMAGE_PIPE_YUV_VIEW_H_
#define GARNET_EXAMPLES_UI_YUV_TO_IMAGE_PIPE_YUV_VIEW_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/component/cpp/startup_context.h>
#include <src/lib/fxl/logging.h>
#include <src/lib/fxl/macros.h>
#include <lib/ui/base_view/cpp/base_view.h>
#include <lib/ui/base_view/cpp/v1_base_view.h>
#include <lib/ui/scenic/cpp/resources.h>

// Displays a YUV frame via ImagePipe using given PixelFormat, to allow visual
// inspection that a given PixelFormat is being displayed properly by Scenic.
class YuvView : public scenic::BaseView {
 public:
  YuvView(scenic::ViewContext context,
          fuchsia::images::PixelFormat pixel_format);

  ~YuvView() override;

 private:
  // |scenic::BaseView|
  // Called when the scene is "invalidated". Invalidation happens when surface
  // dimensions or metrics change, but not necessarily when surface contents
  // change.
  void OnSceneInvalidated(
      fuchsia::images::PresentationInfo presentation_info) override;

  // |scenic::SessionListener|
  void OnScenicError(std::string error) override {
    FXL_LOG(ERROR) << "Scenic Error " << error;
  }

  void StartYuv();

  void SetVmoPixels(uint8_t* vmo_base);
  void SetBgra8Pixels(uint8_t* vmo_base);
  void SetYuy2Pixels(uint8_t* vmo_base);
  void SetNv12Pixels(uint8_t* vmo_base);
  void SetYv12Pixels(uint8_t* vmo_base);

  // The return value is double so we can potentially generate nice gradients
  // for bit depths higher than 8.
  double GetYValue(double x, double y);
  double GetUValue(double x, double y);
  double GetVValue(double x, double y);

  scenic::ShapeNode node_;

  fidl::InterfacePtr<fuchsia::images::ImagePipe> image_pipe_;

  fuchsia::images::PixelFormat pixel_format_ =
      fuchsia::images::PixelFormat::NV12;
  uint32_t stride_ = 0;

  FXL_DISALLOW_COPY_AND_ASSIGN(YuvView);
};

#endif  // GARNET_EXAMPLES_UI_YUV_TO_IMAGE_PIPE_YUV_VIEW_H_
