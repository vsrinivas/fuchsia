// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_EXAMPLES_VKPROTO_COMMON_SURFACE_PHYS_DEVICE_PARAMS_H_
#define SRC_GRAPHICS_EXAMPLES_VKPROTO_COMMON_SURFACE_PHYS_DEVICE_PARAMS_H_

#include <vulkan/vulkan.hpp>

/**
 * SurfacePhysDeviceParams serves as InitParams for several classes that
 * take a surface and physical device as constructor arguments that need
 * to later use then release these at Init() time.
 */
struct SurfacePhysDeviceParams {
  SurfacePhysDeviceParams(const vk::PhysicalDevice &phys_device, const VkSurfaceKHR &surface)
      : phys_device_(phys_device), surface_(surface) {}

  const vk::PhysicalDevice phys_device_;
  const VkSurfaceKHR surface_;
};

#endif  // SRC_GRAPHICS_EXAMPLES_VKPROTO_COMMON_SURFACE_PHYS_DEVICE_PARAMS_H_
