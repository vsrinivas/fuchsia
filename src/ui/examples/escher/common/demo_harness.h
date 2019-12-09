// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_EXAMPLES_ESCHER_COMMON_DEMO_HARNESS_H_
#define SRC_UI_EXAMPLES_ESCHER_COMMON_DEMO_HARNESS_H_

#include <cstdint>
#include <memory>

#include "src/ui/lib/escher/escher.h"
#include "src/ui/lib/escher/fs/hack_filesystem.h"
#include "src/ui/lib/escher/resources/resource_manager.h"
#include "src/ui/lib/escher/util/stopwatch.h"
#include "src/ui/lib/escher/vk/vulkan_context.h"
#include "src/ui/lib/escher/vk/vulkan_device_queues.h"
#include "src/ui/lib/escher/vk/vulkan_instance.h"
#include "src/ui/lib/escher/vk/vulkan_swapchain.h"
#include "src/ui/lib/escher/vk/vulkan_swapchain_helper.h"

#include <vulkan/vulkan.hpp>

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

  static std::unique_ptr<DemoHarness> New(DemoHarness::WindowParams window_params,
                                          InstanceParams instance_params);
  virtual ~DemoHarness();

  const WindowParams& GetWindowParams() const { return window_params_; }
  escher::VulkanContext GetVulkanContext();
  escher::VulkanSwapchain GetVulkanSwapchain() { return swapchain_; }
  const escher::VulkanDeviceQueuesPtr& device_queues() const { return device_queues_; }
  const escher::HackFilesystemPtr& filesystem() const { return filesystem_; }

  // Notify the demo that it should stop looping and quit.
  void SetShouldQuit() { should_quit_ = true; }
  bool ShouldQuit() { return should_quit_; }

  // Start scheduling/rendering frames until SetShouldQuit() is called.
  void Run(Demo* demo);
  Demo* GetRunningDemo() { return demo_; }

  // Must be called before harness is destroyed.
  void Shutdown();

  escher::Escher* escher() { return escher_.get(); }

 protected:
  // Create via DemoHarness::New().
  DemoHarness(WindowParams window_params);

  // Draw a frame, unless too many unfinished frames are in flight.  Return
  // true if a frame was drawn and false otherwise.
  bool MaybeDrawFrame();

  // |key| must contain either a single alpha-numeric character (uppercase
  // only), or one of the special values "ESCAPE", "SPACE", and "RETURN".
  // Return true if the key-press was handled, and false otherwise.
  bool HandleKeyPress(std::string key);

  // Subclasses are responsible for setting this, as the filesystem on Fuchsia
  // can take a debug_dir to support hot reload.
  escher::HackFilesystemPtr filesystem_;

  vk::Device device() const { return device_queues_->vk_device(); }
  vk::PhysicalDevice physical_device() const { return device_queues_->vk_physical_device(); }
  vk::Instance instance() const { return instance_->vk_instance(); }
  vk::SurfaceKHR surface() const { return device_queues_->vk_surface(); }
  vk::Queue main_queue() const { return device_queues_->vk_main_queue(); }
  uint32_t main_queue_family() const { return device_queues_->vk_main_queue_family(); }
  vk::Queue transfer_queue() const { return device_queues_->vk_transfer_queue(); }
  uint32_t transfer_queue_family() const { return device_queues_->vk_transfer_queue_family(); }

  const escher::VulkanInstance::ProcAddrs& instance_proc_addrs() const {
    return instance_->proc_addrs();
  }

  // Subclasses must implement.  Contains platform-specific logic for scheduling frames.
  virtual void RunForPlatform(Demo* demo) = 0;

  // Called by Run(), before RunForPlatform().
  void BeginRun(Demo* demo);
  // Called by Run(), after RunForPlatform().
  void EndRun();

 private:
  // Called by New() after instantiation is complete, so that virtual functions
  // can be called upon the harness.
  void Init(DemoHarness::InstanceParams instance_params);

  // Called by Init(), in this order.
  virtual void InitWindowSystem() = 0;
  void CreateInstance(InstanceParams params);
  virtual vk::SurfaceKHR CreateWindowAndSurface(const WindowParams& window_params) = 0;
  void CreateDeviceAndQueue(escher::VulkanDeviceQueues::Params params);
  void CreateEscher();
  void CreateSwapchain();

  // Called by Init() via CreateInstance() and CreateDeviceAndQueue().
  virtual void AppendPlatformSpecificInstanceExtensionNames(InstanceParams* params) = 0;
  virtual void AppendPlatformSpecificDeviceExtensionNames(std::set<std::string>* names) = 0;

  // Called by Shutdown(), in this order.
  void DestroySwapchain();
  void DestroyEscher();
  void DestroyDevice();
  void DestroyInstance();
  virtual void ShutdownWindowSystem() = 0;

  double ComputeFps();

  // Vulkan validation reporting.
  VkBool32 HandleDebugReport(VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objectType,
                             uint64_t object, size_t location, int32_t messageCode,
                             const char* pLayerPrefix, const char* pMessage);
  static VkBool32 RedirectDebugReport(VkDebugReportFlagsEXT flags,
                                      VkDebugReportObjectTypeEXT objectType, uint64_t object,
                                      size_t location, int32_t messageCode,
                                      const char* pLayerPrefix, const char* pMessage,
                                      void* pUserData) {
    return reinterpret_cast<DemoHarness*>(pUserData)->HandleDebugReport(
        flags, objectType, object, location, messageCode, pLayerPrefix, pMessage);
  }

  // Tracking frames in flight.
  void OnFrameCreated();
  void OnFrameDestroyed();
  bool IsAtMaxOutstandingFrames();
  uint32_t outstanding_frames_ = 0;
  uint64_t frame_count_ = 0;
  uint64_t first_frame_microseconds_ = 0;
  bool enable_gpu_logging_ = false;

  // Used for FPS calculations.
  escher::Stopwatch stopwatch_;

  Demo* demo_ = nullptr;

  WindowParams window_params_;

  escher::VulkanInstancePtr instance_;
  escher::VulkanDeviceQueuesPtr device_queues_;
  escher::EscherUniquePtr escher_;

  escher::VulkanSwapchain swapchain_;
  std::unique_ptr<escher::VulkanSwapchainHelper> swapchain_helper_;
  uint32_t swapchain_image_count_ = 0;

  bool should_quit_ = false;
  bool shutdown_complete_ = false;
  bool run_offscreen_benchmark_ = false;
};

#endif  // SRC_UI_EXAMPLES_ESCHER_COMMON_DEMO_HARNESS_H_
