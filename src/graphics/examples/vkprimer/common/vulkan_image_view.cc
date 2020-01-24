// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vulkan_image_view.h"

#include "utils.h"

VulkanImageView::VulkanImageView(std::shared_ptr<VulkanLogicalDevice> device,
                                 std::shared_ptr<VulkanPhysicalDevice> phys_device,
                                 const vk::Extent2D &extent)
    : initialized_(false), device_(device), phys_device_(phys_device), extent_(extent) {}

bool VulkanImageView::Init() {
  if (initialized_ == true) {
    RTN_MSG(false, "VulkanImageView is already initialized.\n");
  }

  const auto &device = device_->device();
  format_ = vk::Format::eB8G8R8A8Unorm;

  // Create vk::Image.
  vk::ImageCreateInfo info;
  info.extent = vk::Extent3D(extent_.width, extent_.height, 1);
  info.arrayLayers = 1;
  info.format = format_;
  info.imageType = vk::ImageType::e2D;
  info.initialLayout = vk::ImageLayout::eUndefined;
  info.tiling = vk::ImageTiling::eLinear;
  info.mipLevels = 1;
  info.samples = vk::SampleCountFlagBits::e1;
  info.sharingMode = vk::SharingMode::eExclusive;
  info.usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc;
  auto rv = device->createImageUnique(info);
  if (vk::Result::eSuccess != rv.result) {
    RTN_MSG(false, "VK Error: 0x%x - Failed to create image.", rv.result);
  }
  image_ = std::move(rv.value);

  // Allocate memory for |image_| and bind it.
  auto image_memory_requirements = device->getImageMemoryRequirements(*image_);
  vk::MemoryAllocateInfo alloc_info;
  alloc_info.allocationSize = image_memory_requirements.size;
  alloc_info.memoryTypeIndex = vkp::FindMemoryIndex(
      phys_device_->phys_device(), image_memory_requirements.memoryTypeBits,
      vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
  auto alloc_rv = device->allocateMemoryUnique(alloc_info);
  if (vk::Result::eSuccess != alloc_rv.result) {
    RTN_MSG(false, "VK Error: 0x%x - Failed to allocate device memory for image.", alloc_rv.result);
  }
  image_memory_ = std::move(alloc_rv.value);
  auto bind_rv = device->bindImageMemory(*image_, *image_memory_, 0);
  if (vk::Result::eSuccess != bind_rv) {
    RTN_MSG(false, "VK Error: 0x%x - Failed to bind device memory to image.", bind_rv);
  }

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
  auto rvv = device->createImageViewUnique(view_info);
  if (vk::Result::eSuccess != rvv.result) {
    RTN_MSG(false, "VK Error: 0x%x - Failed to create image view.", rv.result);
  }
  view_ = std::move(rvv.value);

  initialized_ = true;

  return initialized_;
}
