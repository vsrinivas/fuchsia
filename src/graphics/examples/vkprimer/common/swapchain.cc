// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "swapchain.h"

#include <limits>
#include <set>
#include <string>
#include <unordered_map>

#include "src/graphics/examples/vkprimer/common/utils.h"

namespace {

vk::SurfaceFormatKHR ChooseSwapSurfaceFormat(
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

vk::PresentModeKHR ChooseSwapPresentMode(
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

  RTN_IF_MSG(best_mode, (best_mode == kLastPresentMode), "Unable to find usable VkPresentMode.\n");

  return best_mode;
}

vk::Extent2D ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) {
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

bool CreateImageViews(const vk::Device device, const vk::Format& image_format,
                      const std::vector<vk::Image> images,
                      std::vector<vk::UniqueImageView>* image_views) {
  vk::ImageSubresourceRange range;
  range.aspectMask = vk::ImageAspectFlagBits::eColor;
  range.layerCount = 1;
  range.levelCount = 1;

  vk::ImageViewCreateInfo image_view_info;
  image_view_info.format = image_format;
  image_view_info.subresourceRange = range;
  image_view_info.viewType = vk::ImageViewType::e2D;
  for (const auto& image : images) {
    image_view_info.image = image;

    auto [r_image_view, image_view] = device.createImageViewUnique(image_view_info);
    RTN_IF_VKH_ERR(false, r_image_view, "Failed to create image view.\n");
    image_views->emplace_back(std::move(image_view));
  }
  return true;
}

}  // namespace

namespace vkp {

Swapchain::Swapchain(const vk::PhysicalDevice phys_device, std::shared_ptr<vk::Device> device,
                     std::shared_ptr<Surface> vkp_surface)
    : initialized_(false), device_(device), vkp_surface_(std::move(vkp_surface)) {
  phys_device_ = std::make_unique<vk::PhysicalDevice>(phys_device);
}

bool Swapchain::Init() {
  RTN_IF_MSG(false, initialized_, "Swapchain is already initialized.\n");
  RTN_IF_MSG(false, !device_, "Device must be initialized.\n");

  Swapchain::Info info;
  QuerySwapchainSupport(*phys_device_, vkp_surface_->get(), &info);
  vk::SurfaceFormatKHR surface_format = ChooseSwapSurfaceFormat(info.formats);
  vk::PresentModeKHR present_mode = ChooseSwapPresentMode(info.present_modes);
  extent_ = ChooseSwapExtent(info.capabilities);

  uint32_t num_images = info.capabilities.minImageCount + 1;
  if (info.capabilities.maxImageCount > 0 && num_images > info.capabilities.maxImageCount) {
    num_images = info.capabilities.maxImageCount;
  }

  vk::SwapchainCreateInfoKHR swapchain_info;
  swapchain_info.clipped = VK_TRUE;
  swapchain_info.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
  swapchain_info.minImageCount = num_images;
  swapchain_info.imageArrayLayers = 1;
  swapchain_info.imageColorSpace = surface_format.colorSpace;
  swapchain_info.imageExtent = extent_;
  swapchain_info.imageFormat = surface_format.format;
  swapchain_info.imageSharingMode = vk::SharingMode::eExclusive;
  swapchain_info.imageUsage = vk::ImageUsageFlagBits::eColorAttachment;
  swapchain_info.presentMode = present_mode;
  swapchain_info.preTransform = info.capabilities.currentTransform;
  swapchain_info.surface = vkp_surface_->get();

  auto [r_swapchain, swapchain] = device_->createSwapchainKHRUnique(swapchain_info);
  RTN_IF_VKH_ERR(false, r_swapchain, "Failed to create swap chain.\n");
  swap_chain_ = std::move(swapchain);

  auto [r_images, images] = device_->getSwapchainImagesKHR(*swap_chain_);
  RTN_IF_VKH_ERR(false, r_images, "Failed to get swap chain images.\n");

  image_format_ = surface_format.format;

  if (!CreateImageViews(*device_, image_format_, images, &image_views_)) {
    RTN_MSG(false, "Failed to create image views.\n");
  }

  phys_device_.reset();
  initialized_ = true;
  return initialized_;
}

bool Swapchain::QuerySwapchainSupport(vk::PhysicalDevice phys_device, VkSurfaceKHR surface,
                                      Swapchain::Info* info) {
  RTN_IF_VKH_ERR(false, phys_device.getSurfaceCapabilitiesKHR(surface, &info->capabilities),
                 "Failed to get surface capabilities\n");

  auto [r_surface_formats, surface_formats] = phys_device.getSurfaceFormatsKHR(surface);
  RTN_IF_VKH_ERR(false, r_surface_formats, "Failed to get surface formats.\n");
  info->formats = surface_formats;

  auto [r_present_modes, present_modes] = phys_device.getSurfacePresentModesKHR(surface);
  RTN_IF_VKH_ERR(false, r_present_modes, "Failed to get present modes.\n");
  info->present_modes = present_modes;

  return true;
}

}  // namespace vkp
