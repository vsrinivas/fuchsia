// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vulkan_image_view.h"

#include "utils.h"

VulkanImageView::VulkanImageView(std::shared_ptr<VulkanLogicalDevice> device,
    const vk::Extent2D &extent)
    : initialized_(false), device_(device), extent_(extent) {
}

bool VulkanImageView::Init() {
  if (initialized_ == true) {
    RTN_MSG(false, "VulkanImageView is already initialized.\n");
  }

  format_ = vk::Format::eB8G8R8A8Unorm;

  vk::ImageCreateInfo info;
  info.extent = vk::Extent3D(extent_.width, extent_.height, 1);
  info.format = format_;
  info.imageType = vk::ImageType::e2D;
  info.tiling = vk::ImageTiling::eLinear;
  info.mipLevels = 1;
  info.sharingMode = vk::SharingMode::eExclusive;
  info.usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc |
      vk::ImageUsageFlagBits::eTransferDst;
  auto rv = device_->device()->createImageUnique(info);
  if (vk::Result::eSuccess != rv.result) {
    RTN_MSG(false, "VK Error: 0x%x - Failed to create image.", rv.result);
  }
  image_ = std::move(rv.value);

  vk::ImageSubresourceRange range;
  range.aspectMask = vk::ImageAspectFlagBits::eColor;
  range.layerCount = 1;
  range.levelCount = 1;

  vk::ImageViewCreateInfo view_info;
  view_info.format = format_;
  view_info.subresourceRange = range;
  view_info.viewType = vk::ImageViewType::e2D;
  view_info.image = *image_;
  auto rvv = device_->device()->createImageViewUnique(view_info);
  if (vk::Result::eSuccess != rvv.result) {
    RTN_MSG(false, "VK Error: 0x%x - Failed to create image view.", rv.result);
  }
  view_ = std::move(rvv.value);

  initialized_ = true;

  return initialized_;
}
