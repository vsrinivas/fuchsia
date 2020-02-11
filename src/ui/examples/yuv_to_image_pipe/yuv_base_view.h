// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_EXAMPLES_YUV_TO_IMAGE_PIPE_YUV_BASE_VIEW_H_
#define SRC_UI_EXAMPLES_YUV_TO_IMAGE_PIPE_YUV_BASE_VIEW_H_

#include <fuchsia/sysmem/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/ui/scenic/cpp/resources.h>

#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/ui/base_view/base_view.h"

namespace yuv_to_image_pipe {

// Displays a YUV frame via ImagePipe using given PixelFormat, to allow visual
// inspection that a given PixelFormat is being displayed properly by Scenic.
class YuvBaseView : public scenic::BaseView {
 public:
  YuvBaseView(scenic::ViewContext context, fuchsia::sysmem::PixelFormatType pixel_format);
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
  void OnScenicError(std::string error) override { FXL_LOG(ERROR) << "Scenic Error " << error; }

  void SetVmoPixels(uint8_t* vmo_base, uint8_t pixel_multiplier);
  void SetBgra32Pixels(uint8_t* vmo_base, uint8_t pixel_multiplier);
  void SetRgba32Pixels(uint8_t* vmo_base, uint8_t pixel_multiplier);
  void SetNv12Pixels(uint8_t* vmo_base, uint8_t pixel_multiplier);
  void SetI420Pixels(uint8_t* vmo_base, uint8_t pixel_multiplier);

  // The return value is double so we can potentially generate nice gradients
  // for bit depths higher than 8.
  double GetYValue(double x, double y);
  double GetUValue(double x, double y);
  double GetVValue(double x, double y);

  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator_;
  fidl::InterfacePtr<fuchsia::images::ImagePipe2> image_pipe_;
  struct ImageVmo {
    ImageVmo(uint8_t* vmo_ptr, size_t image_bytes, bool needs_flush)
        : vmo_ptr(vmo_ptr), image_bytes(image_bytes), needs_flush(needs_flush) {}

    uint8_t* const vmo_ptr;
    const size_t image_bytes;
    const bool needs_flush;
  };
  std::map<uint32_t /* image_id */, ImageVmo> image_vmos_;

  fuchsia::sysmem::PixelFormatType pixel_format_ = fuchsia::sysmem::PixelFormatType::NV12;
  uint32_t stride_ = 0;
  uint32_t next_image_id_ = 0;

  FXL_DISALLOW_COPY_AND_ASSIGN(YuvBaseView);
};

}  // namespace yuv_to_image_pipe

#endif  // SRC_UI_EXAMPLES_YUV_TO_IMAGE_PIPE_YUV_BASE_VIEW_H_
