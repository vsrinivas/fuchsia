// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_VK_VULKAN_DEVICE_QUEUES_H_
#define SRC_UI_LIB_ESCHER_VK_VULKAN_DEVICE_QUEUES_H_

#include <set>
#include <string>

#include "src/lib/fxl/memory/ref_counted.h"
#include "src/ui/lib/escher/util/debug_print.h"
#include "src/ui/lib/escher/vk/vulkan_context.h"
#include "src/ui/lib/escher/vk/vulkan_instance.h"

#include <vulkan/vulkan.hpp>

namespace escher {

class VulkanDeviceQueues;
using VulkanDeviceQueuesPtr = fxl::RefPtr<VulkanDeviceQueues>;

// Convenient wrapper for creating and managing the lifecycle of a VkDevice
// and a set of VkQueues that are suitable for use by Escher.
class VulkanDeviceQueues : public fxl::RefCountedThreadSafe<VulkanDeviceQueues> {
 public:
  // Parameters used to construct a new Vulkan Device and Queues.
  struct Params {
    std::set<std::string> required_extension_names;
    std::set<std::string> desired_extension_names;
    vk::SurfaceKHR surface;

    enum FlagBits {
      // When picking a queue, don't filter out those that do not support presentation.
      kDisableQueueFilteringForPresent = 1 << 0,
      // Create protected capable Vulkan resources.
      kAllowProtectedMemory = 1 << 1,
    };
    using Flags = uint32_t;
    Flags flags = 0;
  };

  // Device capabilities.
  struct Caps {
    uint32_t max_image_width = 0;
    uint32_t max_image_height = 0;
    std::set<vk::Format> depth_stencil_formats;
    std::set<size_t> msaa_sample_counts;
    std::set<std::string> extensions;
    uint32_t device_api_version;
    bool allow_protected_memory;

    vk::PhysicalDeviceFeatures enabled_features;

    // This function returns vk::eSuccess and the format if there is a matching
    // depth-stencil format; otherwise it returns vk::eErrorFeatureNotPresent.
    vk::ResultValue<vk::Format> GetMatchingDepthStencilFormat(
        const std::vector<vk::Format>& formats) const;

    vk::ResultValue<size_t> GetMatchingSampleCount(const std::vector<size_t>& counts) const;

    vk::ResultValue<vk::Format> GetMatchingDepthStencilFormat() const {
      return GetMatchingDepthStencilFormat(
          {vk::Format::eD16UnormS8Uint, vk::Format::eD24UnormS8Uint, vk::Format::eD32SfloatS8Uint});
    }

    vk::ResultValue<vk::Format> GetMatchingDepthFormat() const {
      return GetMatchingDepthStencilFormat({vk::Format::eD16Unorm, vk::Format::eD32Sfloat});
    }

    std::set<vk::Format> GetAllMatchingDepthStencilFormats(
        const std::set<vk::Format>& formats) const;

    Caps() = default;
    Caps(vk::PhysicalDevice device);
  };

  // Contains dynamically-obtained addresses of device-specific functions.
  struct ProcAddrs {
    PFN_vkCreateSwapchainKHR CreateSwapchainKHR = nullptr;
    PFN_vkDestroySwapchainKHR DestroySwapchainKHR = nullptr;
    PFN_vkGetSwapchainImagesKHR GetSwapchainImagesKHR = nullptr;
    PFN_vkAcquireNextImageKHR AcquireNextImageKHR = nullptr;
    PFN_vkQueuePresentKHR QueuePresentKHR = nullptr;
  };

  // Constructor.
  static fxl::RefPtr<VulkanDeviceQueues> New(VulkanInstancePtr instance, Params params);

  ~VulkanDeviceQueues();

  // Enumerate the available extensions for the specified physical device.
  // Return true if all required extensions are present, and false otherwise.
  // NOTE: if an extension isn't found at first, we look in all required layers
  // to see if it is implemented there.
  static bool ValidateExtensions(vk::PhysicalDevice device,
                                 const std::set<std::string>& required_extension_names,
                                 const std::set<std::string>& required_layer_names);

  vk::Device vk_device() const { return device_; }
  vk::PhysicalDevice vk_physical_device() const { return physical_device_; }
  vk::Queue vk_main_queue() const { return main_queue_; }
  uint32_t vk_main_queue_family() const { return main_queue_family_; }
  vk::Queue vk_transfer_queue() const { return transfer_queue_; }
  uint32_t vk_transfer_queue_family() const { return transfer_queue_family_; }
  vk::SurfaceKHR vk_surface() const { return params_.surface; }
  const vk::DispatchLoaderDynamic& dispatch_loader() const { return dispatch_loader_; }

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
  VulkanDeviceQueues(vk::Device device, vk::PhysicalDevice physical_device, vk::Queue main_queue,
                     uint32_t main_queue_family, vk::Queue transfer_queue,
                     uint32_t transfer_queue_family, VulkanInstancePtr instance, Params params,
                     Caps caps);

  vk::Device device_;
  vk::PhysicalDevice physical_device_;
  vk::DispatchLoaderDynamic dispatch_loader_;
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

ESCHER_DEBUG_PRINTABLE(VulkanDeviceQueues::Caps);

};  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_VK_VULKAN_DEVICE_QUEUES_H_
