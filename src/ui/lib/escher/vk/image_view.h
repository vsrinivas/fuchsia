// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_VK_IMAGE_VIEW_H_
#define SRC_UI_LIB_ESCHER_VK_IMAGE_VIEW_H_

#include "src/ui/lib/escher/forward_declarations.h"
#include "src/ui/lib/escher/resources/resource.h"
#include "src/ui/lib/escher/vk/image.h"

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
            vk::ImageAspectFlags aspect_mask = vk::ImageAspectFlags());
  ~ImageView() override;

  static ImageViewPtr New(ImagePtr image, vk::ImageAspectFlags aspect_mask =
                                              vk::ImageAspectFlags());

  // TODO(ES-83): unfortunately we can't just get the recycler from
  // image->escher(), because that is null for Vulkan swapchain images.
  static ImageViewPtr New(
      ResourceRecycler* resource_recycler, ImagePtr image,
      vk::ImageAspectFlags aspect_mask = vk::ImageAspectFlags());

  const ImagePtr& image() const { return image_; }
  vk::ImageView vk() const { return image_view_; }

  // TODO(ES-83): for a depth-stencil texture, we may want to sample the
  // depth as floating point and the stencil as integer.  In such cases, we
  // could return a separate view for each, e.g.
  //   return float_image_view_ ? float_image_view_ : image_view_.
  vk::ImageView vk_float_view() const { return image_view_; }
  vk::ImageView vk_integer_view() const { return image_view_; }

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

#endif  // SRC_UI_LIB_ESCHER_VK_IMAGE_VIEW_H_
