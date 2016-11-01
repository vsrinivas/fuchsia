// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/renderer/image.h"

namespace escher {

Image::Image(vk::Image image,
             vk::Format format,
             uint32_t width,
             uint32_t height)
    : impl::Resource(nullptr),
      image_(image),
      format_(format),
      width_(width),
      height_(height) {}

Image::~Image() {}

bool Image::HasStencilComponent() const {
  // TODO: are these the only stencil formats?  How do we future-proof this
  // in case more are added?
  return format_ == vk::Format::eD32SfloatS8Uint ||
         format_ == vk::Format::eD24UnormS8Uint;
}

}  // namespace escher
