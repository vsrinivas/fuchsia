// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/vk/image_view.h"

#include "lib/escher/impl/command_buffer.h"
#include "lib/escher/impl/vulkan_utils.h"
#include "lib/escher/resources/resource_recycler.h"
#include "lib/escher/vk/image.h"

namespace escher {

const ResourceTypeInfo ImageView::kTypeInfo("ImageView",
                                            ResourceType::kResource,
                                            ResourceType::kImageView);

ImageView::ImageView(ResourceRecycler* resource_recycler, ImagePtr image,
                     vk::ImageAspectFlags aspect_mask)
    : Resource(resource_recycler),
      image_(std::move(image)),
      width_(image_->width()),
      height_(image_->height()) {
  vk::ImageViewCreateInfo view_info;
  view_info.viewType = vk::ImageViewType::e2D;
  view_info.subresourceRange.baseMipLevel = 0;
  view_info.subresourceRange.levelCount = 1;
  view_info.subresourceRange.baseArrayLayer = 0;
  view_info.subresourceRange.layerCount = 1;
  view_info.subresourceRange.aspectMask = aspect_mask;
  view_info.format = image_->format();
  view_info.image = image_->vk();
  image_view_ =
      ESCHER_CHECKED_VK_RESULT(vk_device().createImageView(view_info));
}

ImageView::~ImageView() { vk_device().destroyImageView(image_view_); }

ImageViewPtr ImageView::New(ResourceRecycler* resource_recycler, ImagePtr image,
                            vk::ImageAspectFlags aspect_mask) {
  return fxl::MakeRefCounted<ImageView>(resource_recycler, std::move(image),
                                        aspect_mask);
}

}  // namespace escher
