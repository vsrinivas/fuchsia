// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_TESTS_IMAGE_PIPE_UNITTEST_COMMON_H_
#define SRC_UI_SCENIC_LIB_GFX_TESTS_IMAGE_PIPE_UNITTEST_COMMON_H_

#include "src/ui/scenic/lib/gfx/resources/image_pipe.h"

namespace scenic_impl {
namespace gfx {
namespace test {

class FakeImage : public Image {
 public:
  FakeImage(Session* session, ResourceId id, escher::ImagePtr image)
      : Image(session, id, Image::kTypeInfo), image_info_(image->info()) {
    image_ = std::move(image);
  }

  void Accept(class ResourceVisitor*) override {}

  uint32_t update_count_ = 0;

  escher::ImageInfo image_info_;

 private:
  bool UpdatePixels(escher::BatchGpuUploader* gpu_uploader) {
    // Update pixels returns the new dirty state. False will stop additional
    // calls to UpdatePixels() until the image is marked dirty.
    return false;
  }
};  // FakeImage

fxl::RefPtr<fsl::SharedVmo> CreateVmoWithBuffer(size_t buffer_size,
                                                std::unique_ptr<uint8_t[]> buffer_pixels);

fxl::RefPtr<fsl::SharedVmo> CreateVmoWithCheckerboardPixels(size_t w, size_t h);

fuchsia::images::ImageInfo CreateImageInfoForBgra8Image(size_t w, size_t h);

fxl::RefPtr<fsl::SharedVmo> CreateVmoWithGradientPixels(size_t w, size_t h);

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_GFX_TESTS_IMAGE_PIPE_UNITTEST_COMMON_H_
