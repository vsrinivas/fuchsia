// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_ESCHER_COMMON_DEMO_HARNESS_H_
#define GARNET_EXAMPLES_ESCHER_COMMON_DEMO_HARNESS_H_

#include <cstdint>

#include <vulkan/vulkan.hpp>

#include "lib/escher/fs/hack_filesystem.h"
#include "lib/escher/resources/resource_manager.h"
#include "lib/escher/vk/vulkan_context.h"
#include "lib/escher/vk/vulkan_device_queues.h"
#include "lib/escher/vk/vulkan_instance.h"
#include "lib/escher/vk/vulkan_swapchain.h"

class Demo;

// DemoHarness is responsible for initializing Vulkan and its connection to the
// window system, and handling mouse/touch/keyboard input.  Subclasses provide
// platform-specific implementations of this functionality.
class DemoHarness {
 public:
  struct WindowParams {
    std::string window_name;
    uint32_t width = 1024;
    uint32_t height = 1024;
    uint32_t desired_swapchain_image_count = 2;
    bool use_fullscreen = false;
  };

  using InstanceParams = escher::VulkanInstance::Params;

  static std::unique_ptr<DemoHarness> New(
      DemoHarness::WindowParams window_params, InstanceParams instance_params);
  virtual ~DemoHarness();

  const WindowParams& GetWindowParams() const { return window_params_; }
  escher::VulkanContext GetVulkanContext();
  escher::VulkanSwapchain GetVulkanSwapchain() { return swapchain_; }
  const escher::VulkanDeviceQueuesPtr& device_queues() const {
    return device_queues_;
  }
  const escher::HackFilesystemPtr& filesystem() const { return filesystem_; }

  // Notify the demo that it should stop looping and quit.
  void SetShouldQuit() { should_quit_ = true; }
  bool ShouldQuit() { return should_quit_; }

  virtual void Run(Demo* demo) = 0;
  Demo* GetRunningDemo() { return demo_; }

  // Must be called before harness is destroyed.
  void Shutdown();

 protected:
  // Create via DemoHarness::New().
  DemoHarness(WindowParams window_params);

  // Draw a frame, unless too many unfinished frames are in flight.  Return
  // true if a frame was drawn and false otherwise.
  bool DrawFrame();

  // Subclasses are responsible for setting this when they start running a Demo,
  // and setting it back to nullptr when they finish running the Demo.
  Demo* demo_ = nullptr;

  // Subclasses are responsible for setting this, as the filesystem on Fuchsia
  // can take a debug_dir to support hot reload.
  escher::HackFilesystemPtr filesystem_;

  vk::Device device() const { return device_queues_->vk_device(); }
  vk::PhysicalDevice physical_device() const {
    return device_queues_->vk_physical_device();
  }
  vk::Instance instance() const { return instance_->vk_instance(); }
  vk::SurfaceKHR surface() const { return device_queues_->vk_surface(); }
  vk::Queue main_queue() const { return device_queues_->vk_main_queue(); }
  uint32_t main_queue_family() const {
    return device_queues_->vk_main_queue_family();
  }
  vk::Queue transfer_queue() const {
    return device_queues_->vk_transfer_queue();
  }
  uint32_t transfer_queue_family() const {
    return device_queues_->vk_transfer_queue_family();
  }

  const escher::VulkanInstance::ProcAddrs& instance_proc_addrs() const {
    return instance_->proc_addrs();
  }

 private:
  // For wrapping swapchain images in VkImage.
  // TODO: Find a nicer solution.
  class SwapchainImageOwner : public escher::ResourceManager {
   public:
    SwapchainImageOwner();

   private:
    void OnReceiveOwnable(std::unique_ptr<escher::Resource> resource) override;
  };

  // Called by New() after instantiation is complete, so that virtual functions
  // can be called upon the harness.
  void Init(DemoHarness::InstanceParams instance_params);

  // Called by Init().
  virtual void InitWindowSystem() = 0;
  void CreateInstance(InstanceParams params);
  virtual vk::SurfaceKHR CreateWindowAndSurface(
      const WindowParams& window_params) = 0;
  void CreateDeviceAndQueue(escher::VulkanDeviceQueues::Params params);
  void CreateSwapchain();

  // Called by Init() via CreateInstance().
  virtual void AppendPlatformSpecificInstanceExtensionNames(
      InstanceParams* params) = 0;

  // Called by Shutdown().
  void DestroySwapchain();
  void DestroyDevice();
  void DestroyInstance();
  virtual void ShutdownWindowSystem() = 0;

  // Redirect to instance method.
  static VkBool32 RedirectDebugReport(VkDebugReportFlagsEXT flags,
                                      VkDebugReportObjectTypeEXT objectType,
                                      uint64_t object, size_t location,
                                      int32_t messageCode,
                                      const char* pLayerPrefix,
                                      const char* pMessage, void* pUserData) {
    return reinterpret_cast<DemoHarness*>(pUserData)->HandleDebugReport(
        flags, objectType, object, location, messageCode, pLayerPrefix,
        pMessage);
  }

  VkBool32 HandleDebugReport(VkDebugReportFlagsEXT flags,
                             VkDebugReportObjectTypeEXT objectType,
                             uint64_t object, size_t location,
                             int32_t messageCode, const char* pLayerPrefix,
                             const char* pMessage);

  WindowParams window_params_;

  escher::VulkanInstancePtr instance_;
  escher::VulkanDeviceQueuesPtr device_queues_;
  escher::VulkanSwapchain swapchain_;

  VkDebugReportCallbackEXT debug_report_callback_;

  std::unique_ptr<SwapchainImageOwner> swapchain_image_owner_;
  uint32_t swapchain_image_count_ = 0;

  bool should_quit_ = false;
  bool shutdown_complete_ = false;
};

#endif  // GARNET_EXAMPLES_ESCHER_COMMON_DEMO_HARNESS_H_
