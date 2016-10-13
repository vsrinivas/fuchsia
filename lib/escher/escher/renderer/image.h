// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "escher/impl/resource.h"

namespace escher {

class Image : public impl::Resource {
 public:
  // Wraps the provided image, but does not take ownership: the caller is
  // responsible for eventually destroying the vk::Image (after this Image
  // object has been destroyed).
  // TODO: revisit this.  It is not convenient for the client to determine when
  // this image object has been destroyed, because it may be e.g. retained by
  // Escher until no active submissions are using it.
  Image(vk::Image image, vk::Format format, uint32_t width, uint32_t height);
  ~Image();

  vk::Image image() const { return image_; }
  vk::Format format() const { return format_; }
  uint32_t width() const { return width_; }
  uint32_t height() const { return height_; }

 private:
  vk::Image image_;
  vk::Format format_;
  uint32_t width_;
  uint32_t height_;

  FTL_DISALLOW_COPY_AND_ASSIGN(Image);
};

typedef ftl::RefPtr<Image> ImagePtr;

}  // namespace escher
