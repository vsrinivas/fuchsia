// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_UI_SCENIC_CLIENT_HOST_IMAGE_CYCLER_H_
#define LIB_UI_SCENIC_CLIENT_HOST_IMAGE_CYCLER_H_

#include "lib/ui/scenic/client/host_memory.h"

namespace scenic_lib {

// Creates a node which presents double-buffered content drawn to an image
// in host memory.
class HostImageCycler : public scenic_lib::EntityNode {
 public:
  HostImageCycler(scenic_lib::Session* session);
  ~HostImageCycler();

  // Acquires an image for rendering.
  // At most one image can be acquired at a time.
  // The client is responsible for clearing the image.
  const HostImage* AcquireImage(uint32_t width, uint32_t height,
                                uint32_t stride,
                                fuchsia::images::PixelFormat pixel_format =
                                    fuchsia::images::PixelFormat::BGRA_8,
                                fuchsia::images::ColorSpace color_space =
                                    fuchsia::images::ColorSpace::SRGB);

  // Releases the image most recently acquired using |AcquireImage()|.
  // Sets the content node's texture to be backed by the image.
  void ReleaseAndSwapImage();

 private:
  static constexpr uint32_t kNumBuffers = 2u;

  scenic_lib::ShapeNode content_node_;
  scenic_lib::Material content_material_;
  scenic_lib::HostImagePool image_pool_;

  bool acquired_image_ = false;
  bool reconfigured_ = false;
  uint32_t image_index_ = 0u;

  FXL_DISALLOW_COPY_AND_ASSIGN(HostImageCycler);
};

}  // namespace scenic_lib

#endif  // LIB_UI_SCENIC_CLIENT_HOST_IMAGE_CYCLER_H_
