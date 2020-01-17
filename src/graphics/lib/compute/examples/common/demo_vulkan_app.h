// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_EXAMPLES_COMMON_DEMO_VULKAN_APP_H_
#define SRC_GRAPHICS_LIB_COMPUTE_EXAMPLES_COMMON_DEMO_VULKAN_APP_H_

#include <functional>

#include "tests/common/fps_counter.h"
#include "tests/common/vk_app_state.h"
#include "tests/common/vk_swapchain.h"
#include "tests/common/vk_swapchain_queue.h"

// Base class for multiple demo applications that use Spinel or Mold and
// display things using Vulkan. This setups a Vulkan device and swapchain,
// and provides an optional vk_swapchain_queue_t to ease development.
//
// Usage is the following:
//
//    1) Define a derived class from this one (e.g. MyDemo), that overrides
//       the following methods: setup(), teardown() and drawFrame().
//
//    2) Create a MyDemo instance, then call its init() method to setup
//       its window / display surface and associated Vulkan instance,
//       device and swapchains.
//
//    3) Call the run() method, which will end up calling drawFrame()
//       in a loop with a monotonic frame counter argument.
//
class DemoVulkanApp {
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

    // |verbose| enables logs to be dumped during execution.
    // |debug| enables Vulkan validation and adds more logs.
    bool verbose = false;
    bool debug   = false;

    // |disable_vsync| is used to disable vsync synchronization,
    // and |print_fps| prints a frames/second count on stdout
    // every 2 seconds. Enabling these is useful for benchmarking
    // raw rendering performance, but will introduce tearing.
    bool disable_vsync = false;
    bool print_fps     = false;

    // |wanted_format| is the desired swachain image format,
    // VK_FORMAT_UNDEFINED leaves the choice to the swapchain
    // implementation, and is a sane default. Note that init()
    // will fail if the Vulkan swapchain cannot support it.
    VkFormat wanted_format = VK_FORMAT_UNDEFINED;

    // Set to true if this demo requires shaders to write directly to
    // swapchain images. For example when using the Spinel library to
    // render directly into such images.
    bool require_swapchain_image_shader_storage = false;

    // Set to true to enable a swapchain queue. If set, the derived
    // class should call acquireSwapchainQueueImage() and
    // presentSwapchainQueueImage() in its drawFrame() method.
    //
    // If unset (the default), the drawFrame() method should only
    // call acquireSwapchainImage() and presentSwapchainImage().
    bool enable_swapchain_queue = false;

    // The following fields are only used if |enable_swapchain_queue|
    // set, and are used to initialize the swapchain queue.
    // (see vk_swapchain_queue_config_t for details).
    VkRenderPass enable_framebuffers   = VK_NULL_HANDLE;
    uint32_t     sync_semaphores_count = 0u;
  };

  DemoVulkanApp() = default;
  virtual ~DemoVulkanApp();

  // Optional callback to customize the vk_app_state_config_t before
  // calling vk_app_state_create().
  using AppStateConfigCallback = std::function<void(vk_app_state_config_t *)>;

  // Initialize instance. Return true on success. On failure, print error messages
  // to stderr and return false. |app_state_config_callback| can be used to customize
  // the vk_app_state_config_t (e.g. for device selection and/or Spinel target detection).
  bool
  init(const Config & config, AppStateConfigCallback * app_state_config_callback = nullptr);

  // Called to perform swapchain-image specific setup before presentation.
  // Returns true for success, false for failure (in which case run() will
  // exit immediately).
  virtual bool
  setup()
  {
    return true;
  }

  // Called to perform swapchain-image specific teardown after presentation.
  // This is called just before run() exits, except if setup() returned false.
  virtual void
  teardown()
  {
  }

  // Called to draw a single swapchain image. |frame_counter| is a monotonic
  // counter that is incremented on every frame.
  //
  // If the swapchain queue was *not* enabled on construction, |image_index_|
  // will be set to the current swapchain image index, and the method should
  // perform at least one submit that waits on the image acquired semaphore,
  // and signal the image rendered semaphore.
  //
  // If the swapchain queue *was* enabled, |image_index_| and |swapchain_queue_image_|
  // will be set, and the method should only fill the swapchain queue image's command
  // buffer, which will be submitted later by the run() method.
  //
  // Return true on success, or false in case of failure (in which case
  // the rendering loop in run() stops).
  virtual bool
  drawFrame(uint32_t frame_counter)
  {
    // Do nothing by default!
    return true;
  }

  // Run the demo until the end.
  // Calls setup(), then drawFrame() in a loop, then teardown().
  void
  run();

  // Call this function to force-quit the application. Can be called from
  // drawFrame() or setup().
  void
  doQuit();

  // Return current swapchain extent.
  VkExtent2D
  extent() const
  {
    return swapchain_extent_;
  }

 protected:
  // Derived classes should call these functions in their drawFrame()
  // implementation to acquire and present swapchain images.

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

  vk_app_state_t     app_state_                = {};
  VkQueue            graphics_queue_           = VK_NULL_HANDLE;
  vk_swapchain_t *   swapchain_                = nullptr;
  VkSurfaceKHR       swapchain_surface_        = VK_NULL_HANDLE;
  VkSurfaceFormatKHR swapchain_surface_format_ = {};
  VkExtent2D         swapchain_extent_         = {};
  uint32_t           swapchain_image_count_    = 0;

  vk_swapchain_queue_t *             swapchain_queue_       = nullptr;
  const vk_swapchain_queue_image_t * swapchain_queue_image_ = nullptr;

  uint32_t      image_index_ = 0;
  bool          print_fps_   = false;
  fps_counter_t fps_counter_ = {};
  bool          print_ticks_ = false;
  bool          do_quit_     = false;
};

#endif  // SRC_GRAPHICS_LIB_COMPUTE_EXAMPLES_COMMON_DEMO_VULKAN_APP_H_
