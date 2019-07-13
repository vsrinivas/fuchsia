// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vulkan_swapchain.h"

#include <limits>
#include <set>
#include <string>
#include <unordered_map>

#include "utils.h"

namespace {

static vk::SurfaceFormatKHR ChooseSwapSurfaceFormat(
    const std::vector<vk::SurfaceFormatKHR>& available_formats) {
  if (available_formats.size() == 1 && available_formats[0].format == vk::Format::eUndefined) {
    return {vk::Format::eB8G8R8A8Unorm, vk::ColorSpaceKHR::eSrgbNonlinear};
  }

  for (const auto& available_format : available_formats) {
    if (available_format.format == vk::Format::eB8G8R8A8Unorm &&
        available_format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
      return available_format;
    }
  }

  return available_formats[0];
}

static vk::PresentModeKHR ChooseSwapPresentMode(
    const std::vector<vk::PresentModeKHR>& available_present_modes) {
  std::unordered_map<vk::PresentModeKHR, int> kPresentModePriorities = {
      {vk::PresentModeKHR::eFifo, 0},
      {vk::PresentModeKHR::eMailbox, 1},
      {vk::PresentModeKHR::eImmediate, 2},
      {vk::PresentModeKHR::eFifoRelaxed, 3},
  };

  const vk::PresentModeKHR kLastPresentMode = vk::PresentModeKHR::eFifoRelaxed;
  vk::PresentModeKHR best_mode = kLastPresentMode;
  for (const auto& present_mode : available_present_modes) {
    if (kPresentModePriorities[present_mode] < kPresentModePriorities[best_mode]) {
      best_mode = present_mode;
    }
  }

  if (best_mode == kLastPresentMode) {
    RTN_MSG(best_mode, "Unable to find usable VkPresentMode.\n");
  }

  return best_mode;
}

static vk::Extent2D ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) {
  if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
    return capabilities.currentExtent;
  } else {
    vk::Extent2D extent = {1024, 768};
    extent.width = std::max(capabilities.minImageExtent.width,
                            std::min(capabilities.maxImageExtent.width, extent.width));
    extent.height = std::max(capabilities.minImageExtent.height,
                             std::min(capabilities.maxImageExtent.height, extent.height));

    return extent;
  }
}

static bool CreateImageViews(const vk::Device device, const vk::Format& image_format,
                             const std::vector<vk::Image> images,
                             std::vector<vk::UniqueImageView>* image_views) {
  vk::ImageSubresourceRange range;
  range.aspectMask = vk::ImageAspectFlagBits::eColor;
  range.layerCount = 1;
  range.levelCount = 1;

  vk::ImageViewCreateInfo info;
  info.format = image_format;
  info.subresourceRange = range;
  info.viewType = vk::ImageViewType::e2D;
  for (const auto& image : images) {
    info.image = image;

    auto rv = device.createImageViewUnique(info);
    if (vk::Result::eSuccess != rv.result) {
      RTN_MSG(false, "VK Error: 0x%x - Failed to create image view.", rv.result);
    }
    image_views->emplace_back(std::move(rv.value));
  }
  return true;
}

}  // namespace

VulkanSwapchain::VulkanSwapchain(const vk::PhysicalDevice phys_device,
                                 std::shared_ptr<VulkanLogicalDevice> device,
                                 std::shared_ptr<VulkanSurface> surface)
    : initialized_(false), device_(device), surface_(surface) {
  phys_device_ = std::make_unique<vk::PhysicalDevice>(phys_device);
}

bool VulkanSwapchain::Init() {
  if (initialized_ == true) {
    RTN_MSG(false, "VulkanSwapchain is already initialized.\n");
  }

  VulkanSwapchain::Info info;
  QuerySwapchainSupport(*phys_device_, surface_->surface(), &info);
  vk::SurfaceFormatKHR surface_format = ChooseSwapSurfaceFormat(info.formats);
  vk::PresentModeKHR present_mode = ChooseSwapPresentMode(info.present_modes);
  extent_ = ChooseSwapExtent(info.capabilities);

  uint32_t num_images = info.capabilities.minImageCount + 1;
  if (info.capabilities.maxImageCount > 0 && num_images > info.capabilities.maxImageCount) {
    num_images = info.capabilities.maxImageCount;
  }

  vk::SwapchainCreateInfoKHR create_info;
  create_info.clipped = VK_TRUE;
  create_info.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
  create_info.minImageCount = num_images;
  create_info.imageArrayLayers = 1;
  create_info.imageColorSpace = surface_format.colorSpace;
  create_info.imageExtent = extent_;
  create_info.imageFormat = surface_format.format;
  create_info.imageSharingMode = vk::SharingMode::eExclusive;
  create_info.imageUsage = vk::ImageUsageFlagBits::eColorAttachment;
  create_info.presentMode = present_mode;
  create_info.preTransform = info.capabilities.currentTransform;
  create_info.surface = surface_->surface();

  auto rv = device_->device()->createSwapchainKHRUnique(create_info);
  if (vk::Result::eSuccess != rv.result) {
    RTN_MSG(false, "VK Error: 0x%x - Failed to create swap chain.", rv.result);
  }
  swap_chain_ = std::move(rv.value);

  auto rv_images = device_->device()->getSwapchainImagesKHR(*swap_chain_);
  if (vk::Result::eSuccess != rv_images.result) {
    RTN_MSG(false, "VK Error: 0x%x - Failed to get swap chain images.", rv_images.result);
  }
  auto images = rv_images.value;

  image_format_ = surface_format.format;

  if (!CreateImageViews(*device_->device(), image_format_, images, &image_views_)) {
    RTN_MSG(false, "Failed to create image views.\n");
  }

  phys_device_.reset();
  initialized_ = true;
  return initialized_;
}

bool VulkanSwapchain::QuerySwapchainSupport(vk::PhysicalDevice phys_device, VkSurfaceKHR surface,
                                            VulkanSwapchain::Info* info) {
  auto result = phys_device.getSurfaceCapabilitiesKHR(surface, &info->capabilities);
  if (vk::Result::eSuccess != result) {
    RTN_MSG(false, "VK Error: 0x%x - Failed to get surface capabilities.", result);
  }

  auto rv = phys_device.getSurfaceFormatsKHR(surface);
  if (vk::Result::eSuccess != rv.result) {
    RTN_MSG(false, "VK Error: 0x%x - Failed to get surface formats.", rv.result);
  }
  info->formats = rv.value;

  auto rv_present = phys_device.getSurfacePresentModesKHR(surface);
  if (vk::Result::eSuccess != rv_present.result) {
    RTN_MSG(false, "VK Error: 0x%x - Failed to get present modes.", rv_present.result);
  }
  info->present_modes = rv_present.value;

  return true;
}
