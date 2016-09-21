// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#define VULKAN_HPP_NO_EXCEPTIONS
#include <vulkan/vulkan.hpp>

// Dynamically obtains addresses of instance-specific functions needed by demo.
struct InstanceProcAddrs {
  // Initialize all fields to nullptr.
  InstanceProcAddrs();

  // Load addresses from device.
  InstanceProcAddrs(vk::Instance);

  PFN_vkCreateDebugReportCallbackEXT CreateDebugReportCallbackEXT;
  PFN_vkDestroyDebugReportCallbackEXT DestroyDebugReportCallbackEXT;
  PFN_vkGetPhysicalDeviceSurfaceSupportKHR GetPhysicalDeviceSurfaceSupportKHR;
};

// Dynamically obtains addresses of device-specific functions needed by demo.
struct DeviceProcAddrs {
  // Initialize all fields to nullptr.
  DeviceProcAddrs();

  // Load addresses from device.
  DeviceProcAddrs(vk::Device);

  PFN_vkCreateSwapchainKHR CreateSwapchainKHR;
  PFN_vkDestroySwapchainKHR DestroySwapchainKHR;
  PFN_vkGetSwapchainImagesKHR GetSwapchainImagesKHR;
  PFN_vkAcquireNextImageKHR AcquireNextImageKHR;
  PFN_vkQueuePresentKHR QueuePresentKHR;
};
