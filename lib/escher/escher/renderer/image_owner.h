// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "escher/renderer/image.h"
#include "escher/resources/resource_core.h"

namespace escher {

// Only subclasses of ImageOwner are allowed to instantiate new Images.  When an
// Image is destroyed, it is notified and given the opportunity to recycle or
// destroy the Image's underlying resources.  The ImageOwner must outlive all of
// its owned Images.
//
// TODO: consider getting rid of this class... now that it inherits from
// ResourceCoreManager, it doesn't do too much.
class ImageOwner : public ResourceCoreManager {
 public:
  explicit ImageOwner(const VulkanContext& context)
      : ResourceCoreManager(context) {}

 protected:
  // Subclasses use this method to create Images.  |image| must be a valid
  // vk::Image.  |mem| may be null in rare cases.  For example, it is not
  // possible to obtain access to the memory associated with images in a Vulkan
  // swapchain.
  ImagePtr CreateImage(std::unique_ptr<ImageCore> core);
};

}  // namespace escher
