// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstdint>
#include <vulkan/vulkan.hpp>

#include "escher/renderer/image_owner.h"
#include "escher/vk/vulkan_context.h"
#include "escher/vk/vulkan_swapchain.h"

#include "keyboard_handler.h"
#include "vulkan_proc_addrs.h"

class Demo {
 public:
  struct WindowParams {
    uint32_t width = 1024;
    uint32_t height = 1024;
    std::string window_name;
    uint32_t desired_swapchain_image_count = 2;
    bool use_fullscreen = false;
  };

  struct InstanceParams {
    std::vector<std::string> layer_names{"VK_LAYER_LUNARG_standard_validation"};
    std::vector<std::string> extension_names;
  };

  Demo(InstanceParams instance_params, WindowParams window_params) {
    InitWindowSystem();
    CreateInstance(instance_params);
    CreateWindowAndSurface(window_params);
    CreateDeviceAndQueue();
    CreateSwapchain(window_params);
  }

  ~Demo() {
    DestroySwapchain();
    DestroyDevice();
    DestroyInstance();
    ShutdownWindowSystem();
  }

  const std::vector<vk::LayerProperties>& GetInstanceLayers() const {
    return instance_layers_;
  }
  const std::vector<vk::ExtensionProperties>& GetInstanceExtensions() const {
    return instance_extensions_;
  }

  escher::VulkanContext GetVulkanContext();
  escher::VulkanSwapchain GetVulkanSwapchain() { return swapchain_; }

  // Register a callback to fire when |key| is pressed.  Key must contain either
  // a single alpha-numeric character (uppercase only), or one of the special
  // values "ESCAPE", "SPACE", and "RETURN".
  void SetKeyCallback(std::string key, std::function<void()> func);

  // Notify the demo that it should stop looping and quit.
  void SetShouldQuit();
  bool ShouldQuit() { return should_quit_; }

  // Poll for platform-specific events.
  void PollEvents();

 private:
  // For wrapping swapchain images in VkImage.
  // TODO: Find a nicer solution.
  class SwapchainImageOwner : public escher::ImageOwner {
   public:
    explicit SwapchainImageOwner(const escher::VulkanContext& context);
    using escher::ImageOwner::CreateImage;

   private:
    void ReceiveResourceCore(
        std::unique_ptr<escher::ResourceCore> core) override;
  };

  vk::Instance instance_;
  vk::SurfaceKHR surface_;
  // TODO: may not need to retain physical_device_
  vk::PhysicalDevice physical_device_;
  vk::Device device_;
  vk::Queue queue_;
  uint32_t queue_family_index_ = UINT32_MAX;  // initialize to invalid index.
  vk::Queue transfer_queue_;
  uint32_t transfer_queue_family_index_ = UINT32_MAX;  // invalid index.
  escher::VulkanSwapchain swapchain_;

  VkDebugReportCallbackEXT debug_report_callback_;

  InstanceProcAddrs instance_procs_;
  DeviceProcAddrs device_procs_;

  std::unique_ptr<SwapchainImageOwner> swapchain_image_owner_;
  uint32_t swapchain_image_count_ = 0;

  void InitWindowSystem();
  void CreateInstance(InstanceParams params);
  void CreateWindowAndSurface(const WindowParams& window_params);
  void CreateDeviceAndQueue();
  void CreateSwapchain(const WindowParams& window_params);

  void DestroySwapchain();
  void DestroyDevice();
  void DestroyInstance();
  void ShutdownWindowSystem();

  void AppendPlatformSpecificInstanceExtensionNames(InstanceParams* params);

  // Redirect to instance method.
  static VkBool32 RedirectDebugReport(VkDebugReportFlagsEXT flags,
                                      VkDebugReportObjectTypeEXT objectType,
                                      uint64_t object,
                                      size_t location,
                                      int32_t messageCode,
                                      const char* pLayerPrefix,
                                      const char* pMessage,
                                      void* pUserData) {
    return reinterpret_cast<Demo*>(pUserData)->HandleDebugReport(
        flags, objectType, object, location, messageCode, pLayerPrefix,
        pMessage);
  }

  VkBool32 HandleDebugReport(VkDebugReportFlagsEXT flags,
                             VkDebugReportObjectTypeEXT objectType,
                             uint64_t object,
                             size_t location,
                             int32_t messageCode,
                             const char* pLayerPrefix,
                             const char* pMessage);

  std::vector<vk::LayerProperties> instance_layers_;
  std::vector<vk::ExtensionProperties> instance_extensions_;

  vk::InstanceCreateInfo instance_create_info;

  KeyboardHandler keyboard_handler_;
  bool should_quit_ = false;
};
