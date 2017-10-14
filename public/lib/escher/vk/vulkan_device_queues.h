// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <set>
#include <string>
#include <vulkan/vulkan.hpp>

#include "lib/escher/vk/vulkan_context.h"
#include "lib/escher/vk/vulkan_instance.h"
#include "lib/fxl/memory/ref_counted.h"

namespace escher {

class VulkanDeviceQueues;
using VulkanDeviceQueuesPtr = fxl::RefPtr<VulkanDeviceQueues>;

// Convenient wrapper for creating and managing the lifecycle of a VkDevice
// and a set of VkQueues that are suitable for use by Escher.
class VulkanDeviceQueues
    : public fxl::RefCountedThreadSafe<VulkanDeviceQueues> {
 public:
  // Parameters used to construct a new Vulkan Device and Queues.
  struct Params {
    std::set<std::string> extension_names;
    vk::SurfaceKHR surface;
  };

  // Device capabilities.
  struct Caps {
    uint32_t max_image_width = 0;
    uint32_t max_image_height = 0;

    Caps(vk::PhysicalDeviceProperties props);
  };

  // Contains dynamically-obtained addresses of device-specific functions.
  struct ProcAddrs {
    ProcAddrs(vk::Device device, const std::set<std::string>& extension_names);

    PFN_vkCreateSwapchainKHR CreateSwapchainKHR = nullptr;
    PFN_vkDestroySwapchainKHR DestroySwapchainKHR = nullptr;
    PFN_vkGetSwapchainImagesKHR GetSwapchainImagesKHR = nullptr;
    PFN_vkAcquireNextImageKHR AcquireNextImageKHR = nullptr;
    PFN_vkQueuePresentKHR QueuePresentKHR = nullptr;

#if defined(OS_FUCHSIA)
    PFN_vkImportSemaphoreFuchsiaHandleKHR ImportSemaphoreFuchsiaHandleKHR =
        nullptr;
    PFN_vkGetSemaphoreFuchsiaHandleKHR GetSemaphoreFuchsiaHandleKHR = nullptr;
#endif
  };

  // Constructor.
  static fxl::RefPtr<VulkanDeviceQueues> New(VulkanInstancePtr instance,
                                             Params params);

  ~VulkanDeviceQueues();

  // Enumerate the available extensions for the specified physical device.
  // Return true if all required extensions are present, and false otherwise.
  static bool ValidateExtensions(
      vk::PhysicalDevice device,
      const std::set<std::string>& required_extension_names);

  vk::Device vk_device() const { return device_; }
  vk::PhysicalDevice vk_physical_device() const { return physical_device_; }
  vk::Queue vk_main_queue() const { return main_queue_; }
  uint32_t vk_main_queue_family() const { return main_queue_family_; }
  vk::Queue vk_transfer_queue() const { return transfer_queue_; }
  uint32_t vk_transfer_queue_family() const { return transfer_queue_family_; }
  vk::SurfaceKHR vk_surface() const { return params_.surface; }

  // Return the parameters that were used to create this device and queues.
  const Params& params() const { return params_; }

  // Return the capabilities of this device (e.g. max image width/height, etc.).
  const Caps& caps() const { return caps_; }

  // Return per-device functions that were dynamically looked up.
  const ProcAddrs& proc_addrs() const { return proc_addrs_; }

  // Return a VulkanContext, which contains most of the same information as
  // this object, but is what Escher pervasively passes around internally.
  // TODO: Get rid of VulkanContext, and use this object instead.
  VulkanContext GetVulkanContext() const;

 private:
  VulkanDeviceQueues(vk::Device device,
                     vk::PhysicalDevice physical_device,
                     vk::Queue main_queue,
                     uint32_t main_queue_family,
                     vk::Queue transfer_queue,
                     uint32_t transfer_queue_family,
                     VulkanInstancePtr instance,
                     Params params);

  vk::Device device_;
  vk::PhysicalDevice physical_device_;
  vk::Queue main_queue_;
  uint32_t main_queue_family_;
  vk::Queue transfer_queue_;
  uint32_t transfer_queue_family_;
  vk::SurfaceKHR surface_;
  VulkanInstancePtr instance_;
  Params params_;
  Caps caps_;
  ProcAddrs proc_addrs_;
};

};  // namespace escher
