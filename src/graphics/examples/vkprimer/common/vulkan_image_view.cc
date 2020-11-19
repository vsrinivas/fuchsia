// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vulkan_image_view.h"

#include "utils.h"

VulkanImageView::VulkanImageView(std::shared_ptr<vkp::Device> vkp_device,
                                 std::shared_ptr<VulkanPhysicalDevice> phys_device,
                                 const vk::Extent2D &extent)
    : initialized_(false), vkp_device_(vkp_device), phys_device_(phys_device), extent_(extent) {}

bool VulkanImageView::Init() {
  if (initialized_ == true) {
    RTN_MSG(false, "VulkanImageView is already initialized.\n");
  }

  const vk::Device &device = vkp_device_->get();
  format_ = vk::Format::eB8G8R8A8Unorm;

  // Create vk::Image.
  vk::ImageCreateInfo image_info;
  image_info.extent = vk::Extent3D(extent_.width, extent_.height, 1);
  image_info.arrayLayers = 1;
  image_info.format = format_;
  image_info.imageType = vk::ImageType::e2D;
  image_info.initialLayout = vk::ImageLayout::eUndefined;
  image_info.tiling = vk::ImageTiling::eLinear;
  image_info.mipLevels = 1;
  image_info.samples = vk::SampleCountFlagBits::e1;
  image_info.sharingMode = vk::SharingMode::eExclusive;
  image_info.usage =
      vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc;
  auto [r_image, image] = device.createImageUnique(image_info);
  RTN_IF_VKH_ERR(false, r_image, "Failed to create image.\n");
  image_ = std::move(image);

  // Allocate memory for |image_| and bind it.
  auto image_memory_requirements = device.getImageMemoryRequirements(*image_);
  vk::MemoryAllocateInfo alloc_info;
  alloc_info.allocationSize = image_memory_requirements.size;
  alloc_info.memoryTypeIndex = vkp::FindMemoryIndex(
      phys_device_->phys_device(), image_memory_requirements.memoryTypeBits,
      vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
  auto [r_image_memory, image_memory] = device.allocateMemoryUnique(alloc_info);
  RTN_IF_VKH_ERR(false, r_image_memory, "Failed to allocate device memory for image.\n");
  image_memory_ = std::move(image_memory);

  RTN_IF_VKH_ERR(false, device.bindImageMemory(*image_, *image_memory_, 0),
                 "Failed to bind device memory to image.\n");

  // Create vk::ImageView on |image_|.
  vk::ImageSubresourceRange range;
  range.aspectMask = vk::ImageAspectFlagBits::eColor;
  range.layerCount = 1;
  range.levelCount = 1;

  vk::ImageViewCreateInfo view_info;
  view_info.format = format_;
  view_info.subresourceRange = range;
  view_info.viewType = vk::ImageViewType::e2D;
  view_info.image = *image_;
  auto [r_image_view, image_view] = device.createImageViewUnique(view_info);
  RTN_IF_VKH_ERR(false, r_image_view, "Failed to create image view.\n");
  view_ = std::move(image_view);

  initialized_ = true;

  return initialized_;
}
