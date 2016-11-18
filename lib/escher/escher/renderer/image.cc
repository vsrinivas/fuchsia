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
      height_(height),
      has_depth_(false),
      has_stencil_(false) {
  // TODO: How do we future-proof this in case more formats are added?
  switch (format) {
    case vk::Format::eD16Unorm:
    case vk::Format::eX8D24UnormPack32:
    case vk::Format::eD32Sfloat:
      has_depth_ = true;
    case vk::Format::eS8Uint:
      has_stencil_ = true;
    case vk::Format::eD16UnormS8Uint:
    case vk::Format::eD24UnormS8Uint:
    case vk::Format::eD32SfloatS8Uint:
      has_depth_ = true;
      has_stencil_ = true;
    default:
      // No depth or stencil component.
      break;
  }
}

Image::~Image() {}

}  // namespace escher
