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

}  // namespace escher
