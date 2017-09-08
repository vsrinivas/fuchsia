// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

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
  const HostImage* AcquireImage(uint32_t width,
                                uint32_t height,
                                uint32_t stride,
                                scenic::ImageInfo::PixelFormat pixel_format =
                                    scenic::ImageInfo::PixelFormat::BGRA_8,
                                scenic::ImageInfo::ColorSpace color_space =
                                    scenic::ImageInfo::ColorSpace::SRGB);

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

  FTL_DISALLOW_COPY_AND_ASSIGN(HostImageCycler);
};

}  // namespace scenic_lib
