// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_EXAMPLES_COMMON_VULKAN_WINDOW_H_
#define SRC_GRAPHICS_LIB_COMPUTE_EXAMPLES_COMMON_VULKAN_WINDOW_H_

#include <stdint.h>
#include <vulkan/vulkan.h>

// Forward declarations only.
class VulkanDevice;

typedef struct vk_surface               vk_surface_t;
typedef struct vk_swapchain             vk_swapchain_t;
typedef struct vk_swapchain_queue       vk_swapchain_queue_t;
typedef struct vk_swapchain_queue_image vk_swapchain_queue_image_t;

// Simple class used to create a Vulkan-based display window for our demo
// programs. Usage is the following:
//
//   1) Create instance.
//
//   2) Call Init() passing a VulkanWindow::Config struct with appropriate
//      configuration settings describing the use case for properly
//      initializing a Vulkan instance, device and swapchain.
//
//   3) Use helper methods like instance(), device(), swapchain() to get
//      relevant data.
//
class VulkanWindow {
 public:
  // Configuration information used during initialization.
  struct Config
  {
    // Optional application name, displayed in window title.
    const char * app_name = nullptr;

    // Display surface dimensions. Note that the Vulkan swapchain
    // may end up selecting different values in the end. Use the
    // extent() method, after init(), to get the final ones.
    uint32_t window_width  = 1024;
    uint32_t window_height = 1024;

    // |verbose| enables logs to be dumped during window creation.
    // |debug| enables Vulkan validation and adds more logs.
    bool verbose = false;
    bool debug   = false;

    // |disable_vsync| is used to disable vsync synchronization in the
    // swapchain. Must match the value used to initialize the VulkanDevice!!
    bool disable_vsync = false;

    // |wanted_format| is the desired swachain image format,
    // VK_FORMAT_UNDEFINED leaves the choice to the swapchain
    // implementation, and is a sane default. Note that init()
    // will fail if the Vulkan swapchain cannot support it.
    VkFormat wanted_format = VK_FORMAT_UNDEFINED;

    // Set to true if this window requires shaders to write directly to
    // swapchain images. For example when using the Spinel library to
    // render directly into such images.
    bool require_swapchain_image_shader_storage = false;

    // Set to true if this window requires that buffers or images
    // be copied to swapchain images. For example when unsing the Mold
    // library to render into a VkBuffer, then copying it into the
    // swapchain's VkImage.
    bool require_swapchain_transfers = false;

    // If unset (the default), the drawFrame() method should only
    // call acquireSwapchainImage() and presentSwapchainImage().
    bool enable_swapchain_queue = false;

    // The following fields are only used if |enable_swapchain_queue|
    // set, and are used to initialize the swapchain queue.
    // (see vk_swapchain_queue_config_t for details).
    VkRenderPass enable_framebuffers   = VK_NULL_HANDLE;
    uint32_t     sync_semaphores_count = 0u;
  };

  VulkanWindow()
  {
  }

  ~VulkanWindow();

  // Initialize instance. Return true on success. On failure, print error messages
  // to stderr and return false.
  bool
  init(VulkanDevice * device, const Config & config);

  struct Info
  {
    uint32_t           image_count    = 0;
    VkExtent2D         extent         = {};
    VkSurfaceKHR       surface        = {};
    VkSurfaceFormatKHR surface_format = {};
  };

  const VulkanDevice &
  device() const
  {
    return *device_;
  }

  vk_swapchain_t *
  swapchain() const
  {
    return swapchain_;
  }

  const Info &
  info() const
  {
    return info_;
  }

  // Call this in a loop to handle input UI events.
  // Returns true in case of failure, i.e. when it is time to quit.
  bool
  handleUserEvents();

  // Wait until all GPU operations have completed on this device.
  // Should only be called on application exit, once it is sure that no
  // operations is blocked on synchronization on the GPU, or this will
  // freeze the process.
  void
  waitIdle();

  // These functions should only be called if |enable_swapchain_queue| was false
  // during construction.
  bool
  acquireSwapchainImage();
  void
  presentSwapchainImage();

  // These functions should only be called if |enabled_swapchain_queue| was true
  // during construction.
  bool
  acquireSwapchainQueueImage();
  void
  presentSwapchainQueueImage();

  uint32_t
  image_index() const
  {
    return image_index_;
  }

  vk_swapchain_queue_t *
  swapchain_queue() const
  {
    return swapchain_queue_;
  }

 protected:
  VulkanDevice *   device_    = nullptr;
  vk_surface_t *   surface_   = nullptr;
  vk_swapchain_t * swapchain_ = nullptr;
  Info             info_      = {};

  vk_swapchain_queue_t *             swapchain_queue_       = nullptr;
  const vk_swapchain_queue_image_t * swapchain_queue_image_ = nullptr;
  uint32_t                           image_index_           = 0;
};

#endif  // SRC_GRAPHICS_LIB_COMPUTE_EXAMPLES_COMMON_VULKAN_WINDOW_H_
