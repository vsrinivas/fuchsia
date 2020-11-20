// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_EXAMPLES_VKPRIMER_COMMON_IMAGE_VIEW_H_
#define SRC_GRAPHICS_EXAMPLES_VKPRIMER_COMMON_IMAGE_VIEW_H_

#include "src/graphics/examples/vkprimer/common/device.h"
#include "src/graphics/examples/vkprimer/common/physical_device.h"
#include "src/lib/fxl/macros.h"

#include <vulkan/vulkan.hpp>

namespace vkp {

//
// ImageView provides an image view with a backing |image_| and
// |image_memory_| suitable as a color attachment for rendering.
//
class ImageView {
 public:
  ImageView(std::shared_ptr<Device> vkp_device, std::shared_ptr<PhysicalDevice> vkp_phys_device,
            const vk::Extent2D &extent = {1024, 768});
  ImageView() = delete;

  bool Init();

  const vk::Extent2D &extent() const { return extent_; }
  const vk::Format &format() const { return format_; }
  const vk::UniqueDeviceMemory &image_memory() const { return image_memory_; }
  const vk::UniqueImage &image() const { return image_; }
  const vk::ImageView &get() const { return view_.get(); }

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(ImageView);

  bool initialized_;
  std::shared_ptr<Device> vkp_device_;
  std::shared_ptr<PhysicalDevice> vkp_phys_device_;
  vk::UniqueImage image_;
  vk::UniqueDeviceMemory image_memory_;
  vk::Extent2D extent_;
  vk::Format format_;
  vk::UniqueImageView view_;
};

}  // namespace vkp

#endif  // SRC_GRAPHICS_EXAMPLES_VKPRIMER_COMMON_IMAGE_VIEW_H_
