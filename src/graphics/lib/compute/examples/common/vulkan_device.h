// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_EXAMPLES_COMMON_VULKAN_DEVICE_H_
#define SRC_GRAPHICS_LIB_COMPUTE_EXAMPLES_COMMON_VULKAN_DEVICE_H_

#include <functional>

#include "tests/common/vk_app_state.h"
#include "tests/common/vk_swapchain.h"

struct spn_vk_context_create_info;

// Scoped class to initialize and destroy a Vulkan device instance.
class VulkanDevice {
 public:
  // Optional callback to customize the vk_app_state_config_t before
  // calling vk_app_state_create(). Required for Spinel-specific initialization.
  using AppStateConfigCallback = std::function<void(vk_app_state_config_t *)>;

  // Configuration information used during initialization.
  struct Config
  {
    // Optional application name, displayed in window title.
    const char * app_name = nullptr;

    // |verbose| enables logs to be dumped during window creation.
    // |debug| enables Vulkan validation and adds more logs.
    bool verbose = false;
    bool debug   = false;

    // Set to true to enable swapchain-related extensions for this device.
    bool require_swapchain = false;

    // |disable_vsync| is used to disable vsync synchronization in the
    // swapchain.
    bool disable_vsync = false;
  };

  VulkanDevice() = default;

  ~VulkanDevice();

  // Initialize instance. Return true on success. On failure, print error messages
  // to stderr and return false. |config_callback| can be used to customize
  // the vk_app_state_config_t (e.g. for device selection and/or Spinel target detection).
  bool
  init(const Config & config, const AppStateConfigCallback * config_callback = nullptr);

  // Initialize this device for Spinel. Automatically performs Spinel target and hotsort
  // requirement probing and enable the corresponding Vulkan features and extensions.
  // |vendor_id| and |device_id| are used to select a specific GPU on the host system.
  // On success, sets |*create_info| and return true, or false on failure.
  //
  // NOTE: On success, |block_pool_size| and |handle_count| fields in |*create_info|
  // will be set to defaults that should be large enough for moderately complex images,
  // but the caller might want to increase them for really complex scenes.
  bool
  initForSpinel(const Config &               config,
                uint32_t                     vendor_id,
                uint32_t                     device_id,
                spn_vk_context_create_info * create_info);

  const vk_app_state_t &
  vk_app_state() const
  {
    return app_state_;
  }

  vk_app_state_t &
  vk_app_state()
  {
    return app_state_;
  }

  // Getter methods.
  VkInstance
  vk_instance() const
  {
    return app_state_.instance;
  }

  VkDevice
  vk_device() const
  {
    return app_state_.d;
  }

  VkPhysicalDevice
  vk_physical_device() const
  {
    return app_state_.pd;
  }

  const VkAllocationCallbacks *
  vk_allocator() const
  {
    return app_state_.ac;
  }

  uint32_t
  graphics_queue_family() const
  {
    return app_state_.qfi;
  }

  VkQueue
  vk_graphics_queue() const
  {
    return graphics_queue_;
  }

 protected:
  vk_app_state_t app_state_      = {};
  VkQueue        graphics_queue_ = VK_NULL_HANDLE;
};

#endif  // SRC_GRAPHICS_LIB_COMPUTE_EXAMPLES_COMMON_VULKAN_DEVICE_H_
