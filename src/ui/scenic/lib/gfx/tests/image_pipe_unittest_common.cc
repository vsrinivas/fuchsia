// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/tests/image_pipe_unittest_common.h"

#include <lib/images/cpp/images.h>

#include <gtest/gtest.h>

#include "src/ui/lib/escher/util/image_utils.h"
#include "src/ui/scenic/lib/gfx/tests/mocks/util.h"

namespace scenic_impl {
namespace gfx {
namespace test {

fxl::RefPtr<fsl::SharedVmo> CreateVmoWithBuffer(size_t buffer_size,
                                                std::unique_ptr<uint8_t[]> buffer_pixels) {
  auto shared_vmo = CreateSharedVmo(buffer_size);

  memcpy(shared_vmo->Map(), buffer_pixels.get(), buffer_size);
  return shared_vmo;
}

fxl::RefPtr<fsl::SharedVmo> CreateVmoWithCheckerboardPixels(size_t w, size_t h) {
  size_t pixels_size;
  auto pixels = escher::image_utils::NewCheckerboardPixels(w, h, &pixels_size);
  return CreateVmoWithBuffer(pixels_size, std::move(pixels));
}

fuchsia::images::ImageInfo CreateImageInfoForBgra8Image(size_t w, size_t h) {
  fuchsia::images::ImageInfo image_info;
  image_info.pixel_format = fuchsia::images::PixelFormat::BGRA_8;
  image_info.tiling = fuchsia::images::Tiling::LINEAR;
  image_info.width = w;
  image_info.height = h;
  image_info.stride = w * images::StrideBytesPerWidthPixel(image_info.pixel_format);
  return image_info;
}

fxl::RefPtr<fsl::SharedVmo> CreateVmoWithGradientPixels(size_t w, size_t h) {
  size_t pixels_size;
  auto pixels = escher::image_utils::NewGradientPixels(w, h, &pixels_size);
  return CreateVmoWithBuffer(pixels_size, std::move(pixels));
}

std::unique_ptr<ImagePipeUpdater> CreateImagePipeUpdater(Session* session) {
  return std::make_unique<ImagePipeUpdater>(session->session_context().frame_scheduler,
                                            session->session_context().release_fence_signaller);
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
