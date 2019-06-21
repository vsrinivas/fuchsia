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

static VkSurfaceFormatKHR ChooseSwapSurfaceFormat(
    const std::vector<VkSurfaceFormatKHR>& available_formats) {
  if (available_formats.size() == 1 &&
      available_formats[0].format == VK_FORMAT_UNDEFINED) {
    return {VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
  }

  for (const auto& available_format : available_formats) {
    if (available_format.format == VK_FORMAT_B8G8R8A8_UNORM &&
        available_format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
      return available_format;
    }
  }

  return available_formats[0];
}

static VkPresentModeKHR ChooseSwapPresentMode(
    const std::vector<VkPresentModeKHR>& available_present_modes) {
  std::unordered_map<VkPresentModeKHR, int> kPresentModePriorities = {
      {VK_PRESENT_MODE_FIFO_KHR, 0},
      {VK_PRESENT_MODE_MAILBOX_KHR, 1},
      {VK_PRESENT_MODE_IMMEDIATE_KHR, 2},
      {VK_PRESENT_MODE_MAX_ENUM_KHR, 3},
  };

  VkPresentModeKHR best_mode = VK_PRESENT_MODE_MAX_ENUM_KHR;
  for (const auto& present_mode : available_present_modes) {
    if (kPresentModePriorities[present_mode] <
        kPresentModePriorities[best_mode]) {
      best_mode = present_mode;
    }
  }

  if (best_mode == VK_PRESENT_MODE_MAX_ENUM_KHR) {
    RTN_MSG(best_mode, "Unable to find usable VkPresentMode.\n");
  }

  return best_mode;
}

static VkExtent2D ChooseSwapExtent(
    const VkSurfaceCapabilitiesKHR& capabilities) {
  if (capabilities.currentExtent.width !=
      std::numeric_limits<uint32_t>::max()) {
    return capabilities.currentExtent;
  } else {
    VkExtent2D extent = {1024, 768};
    extent.width =
        std::max(capabilities.minImageExtent.width,
                 std::min(capabilities.maxImageExtent.width, extent.width));
    extent.height =
        std::max(capabilities.minImageExtent.height,
                 std::min(capabilities.maxImageExtent.height, extent.height));

    return extent;
  }
}

static bool CreateImageViews(const VkDevice device,
                             const VkFormat& image_format,
                             const std::vector<VkImage> images,
                             std::vector<VkImageView>* image_views) {
  image_views->resize(images.size());

  for (size_t i = 0; i < images.size(); i++) {
    VkImageViewCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .components =
            {
                .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                .a = VK_COMPONENT_SWIZZLE_IDENTITY,
            },
        .format = image_format,
        .image = images[i],
        .subresourceRange =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseArrayLayer = 0,
                .baseMipLevel = 0,
                .layerCount = 1,
                .levelCount = 1,
            },
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
    };

    if (vkCreateImageView(device, &create_info, nullptr,
                          (image_views->data() + i)) != VK_SUCCESS) {
      RTN_MSG(false, "%s", "Failed to create image views.\n");
    }
  }
  return true;
}

}  // namespace

VulkanSwapchain::VulkanSwapchain(const VkPhysicalDevice& phys_device,
                                 std::shared_ptr<VulkanLogicalDevice> device,
                                 const VkSurfaceKHR& surface)
    : initialized_(false), device_(device) {
  params_ = std::make_unique<SurfacePhysDeviceParams>(phys_device, surface);
}

bool VulkanSwapchain::Init() {
  if (initialized_ == true) {
    RTN_MSG(false, "VulkanSwapchain is already initialized.\n");
  }

  VulkanSwapchain::Info info;
  QuerySwapchainSupport(params_->phys_device_, params_->surface_, &info);
  VkSurfaceFormatKHR surface_format = ChooseSwapSurfaceFormat(info.formats);
  VkPresentModeKHR present_mode = ChooseSwapPresentMode(info.present_modes);
  extent_ = ChooseSwapExtent(info.capabilities);

  uint32_t num_images = info.capabilities.minImageCount + 1;
  if (info.capabilities.maxImageCount > 0 &&
      num_images > info.capabilities.maxImageCount) {
    num_images = info.capabilities.maxImageCount;
  }

  VkSwapchainCreateInfoKHR create_info = {
      .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
      .clipped = VK_TRUE,
      .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
      .minImageCount = num_images,
      .imageArrayLayers = 1,
      .imageColorSpace = surface_format.colorSpace,
      .imageExtent = extent_,
      .imageFormat = surface_format.format,
      .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
      .oldSwapchain = VK_NULL_HANDLE,
      .presentMode = present_mode,
      .preTransform = info.capabilities.currentTransform,
      .surface = params_->surface_,
  };

  auto err = vkCreateSwapchainKHR(device_->device(), &create_info, nullptr,
                                  &swap_chain_);
  if (VK_SUCCESS != err) {
    RTN_MSG(false, "VK Error: 0x%x - Failed to create swap chain.\n", err);
  }

  vkGetSwapchainImagesKHR(device_->device(), swap_chain_, &num_images, nullptr);
  std::vector<VkImage> images(num_images);
  vkGetSwapchainImagesKHR(device_->device(), swap_chain_, &num_images,
                          images.data());

  image_format_ = surface_format.format;

  if (!CreateImageViews(device_->device(), image_format_, images,
                        &image_views_)) {
    RTN_MSG(false, "Failed to create image views.\n");
  }

  params_.reset();
  initialized_ = true;
  return initialized_;
}

VulkanSwapchain::~VulkanSwapchain() {
  if (initialized_) {
    vkDestroySwapchainKHR(device_->device(), swap_chain_, nullptr);
    for (size_t i = 0; i < image_views_.size(); i++) {
      vkDestroyImageView(device_->device(), image_views_[i], nullptr);
    }
  }
}

bool VulkanSwapchain::QuerySwapchainSupport(VkPhysicalDevice phys_device,
                                            VkSurfaceKHR surface,
                                            VulkanSwapchain::Info* info) {
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phys_device, surface,
                                            &info->capabilities);

  uint32_t num_formats;
  vkGetPhysicalDeviceSurfaceFormatsKHR(phys_device, surface, &num_formats,
                                       nullptr);

  if (num_formats != 0) {
    info->formats.resize(num_formats);
    vkGetPhysicalDeviceSurfaceFormatsKHR(phys_device, surface, &num_formats,
                                         info->formats.data());
  } else {
    RTN_MSG(false, "%s", "No surface formats.\n");
  }

  uint32_t num_present_modes;
  vkGetPhysicalDeviceSurfacePresentModesKHR(phys_device, surface,
                                            &num_present_modes, nullptr);

  if (num_present_modes != 0) {
    info->present_modes.resize(num_present_modes);
    vkGetPhysicalDeviceSurfacePresentModesKHR(
        phys_device, surface, &num_present_modes, info->present_modes.data());
  } else {
    RTN_MSG(false, "%s", "No present modes.\n");
  }

  return true;
}
