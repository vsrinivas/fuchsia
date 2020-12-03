// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_EXAMPLES_VKPRIMER_COMMON_SWAPCHAIN_H_
#define SRC_GRAPHICS_EXAMPLES_VKPRIMER_COMMON_SWAPCHAIN_H_

#include <vector>

#include "src/graphics/examples/vkprimer/common/device.h"
#include "src/lib/fxl/macros.h"
#ifdef __Fuchsia__
#include "src/graphics/examples/vkprimer/fuchsia/surface.h"
#else
#include "src/graphics/examples/vkprimer/glfw/surface.h"
#endif
#include "src/graphics/examples/vkprimer/common/surface_phys_device_params.h"

#include <vulkan/vulkan.hpp>

namespace vkp {

class Swapchain {
 public:
  struct Info {
    vk::SurfaceCapabilitiesKHR capabilities;
    std::vector<vk::SurfaceFormatKHR> formats;
    std::vector<vk::PresentModeKHR> present_modes;
  };

  Swapchain(vk::PhysicalDevice phys_device, std::shared_ptr<vk::Device> device,
            std::shared_ptr<Surface> vkp_surface);
  Swapchain() = delete;

  bool Init();

  static void AppendRequiredDeviceExtensions(std::vector<const char *> *exts);

  static bool QuerySwapchainSupport(vk::PhysicalDevice phys_device, VkSurfaceKHR surface,
                                    Swapchain::Info *info);

  const vk::Extent2D &extent() const { return extent_; }
  const vk::Format &image_format() const { return image_format_; }
  const std::vector<vk::UniqueImageView> &image_views() const { return image_views_; }
  const vk::SwapchainKHR &get() const { return swap_chain_.get(); }

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(Swapchain);

  bool initialized_;
  std::shared_ptr<vk::Device> device_;
  vk::Extent2D extent_;
  vk::Format image_format_;
  std::vector<vk::UniqueImageView> image_views_;
  std::shared_ptr<Surface> vkp_surface_;
  std::unique_ptr<vk::PhysicalDevice> phys_device_;

  vk::UniqueSwapchainKHR swap_chain_;
};

}  // namespace vkp

#endif  // SRC_GRAPHICS_EXAMPLES_VKPRIMER_COMMON_SWAPCHAIN_H_
