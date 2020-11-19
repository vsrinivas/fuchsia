// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_EXAMPLES_VKPRIMER_COMMON_VULKAN_SWAPCHAIN_H_
#define SRC_GRAPHICS_EXAMPLES_VKPRIMER_COMMON_VULKAN_SWAPCHAIN_H_

#include <vector>

#include "device.h"
#include "src/lib/fxl/macros.h"
#include "surface_phys_device_params.h"
#include "vulkan_surface.h"

#include <vulkan/vulkan.hpp>

class VulkanSwapchain {
 public:
  struct Info {
    vk::SurfaceCapabilitiesKHR capabilities;
    std::vector<vk::SurfaceFormatKHR> formats;
    std::vector<vk::PresentModeKHR> present_modes;
  };

  VulkanSwapchain(vk::PhysicalDevice phys_device, std::shared_ptr<vkp::Device> vkp_device,
                  std::shared_ptr<VulkanSurface> surface);
  VulkanSwapchain() = delete;

  bool Init();

  static void AppendRequiredDeviceExtensions(std::vector<const char *> *exts);

  static bool QuerySwapchainSupport(vk::PhysicalDevice phys_device, VkSurfaceKHR surface,
                                    VulkanSwapchain::Info *info);

  const vk::Extent2D &extent() const { return extent_; }
  const vk::Format &image_format() const { return image_format_; }
  const std::vector<vk::UniqueImageView> &image_views() const { return image_views_; }
  const vk::UniqueSwapchainKHR &swap_chain() const { return swap_chain_; }

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(VulkanSwapchain);

  bool initialized_;
  std::shared_ptr<vkp::Device> vkp_device_;
  vk::Extent2D extent_;
  vk::Format image_format_;
  std::vector<vk::UniqueImageView> image_views_;
  std::shared_ptr<VulkanSurface> surface_;
  std::unique_ptr<vk::PhysicalDevice> phys_device_;

  vk::UniqueSwapchainKHR swap_chain_;
};

#endif  // SRC_GRAPHICS_EXAMPLES_VKPRIMER_COMMON_VULKAN_SWAPCHAIN_H_
