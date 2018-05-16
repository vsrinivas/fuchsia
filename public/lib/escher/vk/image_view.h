// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_VK_IMAGE_VIEW_H_
#define LIB_ESCHER_VK_IMAGE_VIEW_H_

#include "lib/escher/forward_declarations.h"
#include "lib/escher/resources/resource.h"
#include "lib/escher/vk/image.h"

namespace escher {

class ImageView : public Resource {
 public:
  static const ResourceTypeInfo kTypeInfo;
  const ResourceTypeInfo& type_info() const override { return kTypeInfo; }

  // Construct an ImageView, which encapsulates a newly-created VkImageView.
  // |aspect_mask| is used to create the VkImageView, and |resource_recycler|
  // guarantees that the underlying Vulkan resources are not destroyed while
  // still referenced by a pending command buffer.
  ImageView(ResourceRecycler* resource_recycler, ImagePtr image,
            vk::ImageAspectFlags aspect_mask = vk::ImageAspectFlagBits::eColor);
  ~ImageView() override;

  static ImageViewPtr New(
      ResourceRecycler* resource_recycler, ImagePtr image,
      vk::ImageAspectFlags aspect_mask = vk::ImageAspectFlagBits::eColor);

  const ImagePtr& image() const { return image_; }
  vk::ImageView vk() const { return image_view_; }

  uint32_t width() const { return width_; }
  uint32_t height() const { return height_; }

 private:
  ImagePtr image_;
  vk::ImageView image_view_;
  uint32_t width_;
  uint32_t height_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ImageView);
};

typedef fxl::RefPtr<ImageView> ImageViewPtr;

}  // namespace escher

#endif  // LIB_ESCHER_VK_IMAGE_VIEW_H_
