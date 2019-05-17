// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/vk/image_view.h"

#include "src/ui/lib/escher/escher.h"
#include "src/ui/lib/escher/impl/command_buffer.h"
#include "src/ui/lib/escher/impl/vulkan_utils.h"
#include "src/ui/lib/escher/resources/resource_recycler.h"
#include "src/ui/lib/escher/util/image_utils.h"
#include "src/ui/lib/escher/vk/image.h"

namespace escher {

const ResourceTypeInfo ImageView::kTypeInfo("ImageView",
                                            ResourceType::kResource,
                                            ResourceType::kImageView);

ImageView::ImageView(ResourceRecycler* resource_recycler, ImagePtr image,
                     vk::ImageAspectFlags aspect_mask, void* extension_data)
    : Resource(resource_recycler),
      image_(std::move(image)),
      width_(image_->width()),
      height_(image_->height()) {
  if (!aspect_mask) {
    auto pair = image_utils::IsDepthStencilFormat(image_->format());
    if (!pair.first && !pair.second) {
      // Assume color.
      aspect_mask = vk::ImageAspectFlagBits::eColor;
    } else {
      if (pair.first) {
        aspect_mask |= vk::ImageAspectFlagBits::eDepth;
      }
      if (pair.second) {
        aspect_mask |= vk::ImageAspectFlagBits::eStencil;
      }
    }
  }

  vk::ImageViewCreateInfo view_info;
  view_info.pNext = extension_data;
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

ImageViewPtr ImageView::New(ImagePtr image, vk::ImageAspectFlags aspect_mask) {
  FXL_CHECK(image && image->escher());
  return fxl::MakeRefCounted<ImageView>(image->escher()->resource_recycler(),
                                        std::move(image), aspect_mask);
}

ImageViewPtr ImageView::New(ResourceRecycler* recycler, ImagePtr image,
                            vk::ImageAspectFlags aspect_mask) {
  return fxl::MakeRefCounted<ImageView>(recycler, std::move(image),
                                        aspect_mask);
}

}  // namespace escher
