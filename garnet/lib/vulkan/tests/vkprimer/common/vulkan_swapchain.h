// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_VULKAN_TESTS_VKPRIMER_COMMON_VULKAN_SWAPCHAIN_H_
#define GARNET_LIB_VULKAN_TESTS_VKPRIMER_COMMON_VULKAN_SWAPCHAIN_H_

#include <src/lib/fxl/macros.h>

#include <vector>

#include "surface_phys_device_params.h"
#include "vulkan/vulkan.h"
#include "vulkan/vulkan_core.h"
#include "vulkan_logical_device.h"

class VulkanSwapchain {
 public:
  struct Info {
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> present_modes;
  };

  VulkanSwapchain(const VkPhysicalDevice &phys_device,
                  std::shared_ptr<VulkanLogicalDevice> device,
                  const VkSurfaceKHR &surface);
  VulkanSwapchain() = delete;
  ~VulkanSwapchain();

  bool Init();

  static void AppendRequiredDeviceExtensions(std::vector<const char *> *exts);

  static bool QuerySwapchainSupport(VkPhysicalDevice phys_device,
                                    VkSurfaceKHR surface,
                                    VulkanSwapchain::Info *info);

  const VkExtent2D &extent() const { return extent_; }
  const VkFormat &image_format() const { return image_format_; }
  const std::vector<VkImageView> &image_views() const { return image_views_; }
  const VkSwapchainKHR &swap_chain() const { return swap_chain_; }

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(VulkanSwapchain);

  bool initialized_;
  std::shared_ptr<VulkanLogicalDevice> device_;
  VkExtent2D extent_;
  VkFormat image_format_;
  std::vector<VkImageView> image_views_;
  std::unique_ptr<SurfacePhysDeviceParams> params_;
  VkSwapchainKHR swap_chain_;
};

#endif  // GARNET_LIB_VULKAN_TESTS_VKPRIMER_COMMON_VULKAN_SWAPCHAIN_H_
