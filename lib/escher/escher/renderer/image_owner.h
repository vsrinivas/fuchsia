// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "escher/renderer/image.h"

namespace escher {

// Only subclasses of ImageOwner are allowed to instantiate new Images.  When an
// Image is destroyed, it is notified and given the opportunity to recycle or
// destroy the Image's underlying resources.  The ImageOwner must outlive all of
// its owned Images.
class ImageOwner {
 public:
  virtual void RecycleImage(const ImageInfo& info,
                            vk::Image image,
                            impl::GpuMemPtr mem) = 0;

 protected:
  // Subclasses use this method to create Images.  |image| must be a valid
  // vk::Image.  |mem| may be null in rare cases.  For example, it is not
  // possible to obtain access to the memory associated with images in a Vulkan
  // swapchain.
  ImagePtr CreateImage(const ImageInfo& info,
                       vk::Image image,
                       impl::GpuMemPtr mem);
};

}  // namespace escher
