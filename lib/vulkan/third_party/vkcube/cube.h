/*
 * Copyright (c) 2015-2016 The Khronos Group Inc.
 * Copyright (c) 2015-2016 Valve Corporation
 * Copyright (c) 2015-2016 LunarG, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Author: Chia-I Wu <olv@lunarg.com>
 * Author: Courtney Goeltzenleuchter <courtney@LunarG.com>
 * Author: Ian Elliott <ian@LunarG.com>
 * Author: Jon Ashburn <jon@lunarg.com>
 * Author: Gwan-gyeong Mun <elongbug@gmail.com>
 * Author: Tony Barbour <tony@LunarG.com>
 */

#if defined(VK_USE_PLATFORM_XLIB_KHR) || defined(VK_USE_PLATFORM_XCB_KHR)
#include <X11/Xutil.h>
#endif

#if defined(MAGMA_USE_SHIM)
#include "vulkan_shim.h"
#else
#include <vulkan/vulkan.h>
#endif

#if defined(CUBE_USE_IMAGE_PIPE)
#include <lib/async-loop/cpp/loop.h>

#include "lib/component/cpp/connect.h"
#include "lib/component/cpp/startup_context.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/log_settings.h"
#include "lib/fxl/logging.h"

#include <fuchsia/ui/gfx/cpp/fidl.h>
#include "lib/ui/scenic/cpp/resources.h"
#include "lib/ui/scenic/cpp/session.h"
#include "lib/ui/scenic/fidl_helpers.h"
#include "lib/ui/scenic/types.h"
#include "lib/ui/view_framework/view_provider_service.h"
#include "vkcube_view.h"
#endif  // defined(CUBE_USE_IMAGE_PIPE)

#include "linmath.h"

#define DEMO_TEXTURE_COUNT 1
#define APP_SHORT_NAME "cube"
#define APP_LONG_NAME "The Vulkan Cube Demo Program"

// Allow a maximum of two outstanding presentation operations.
#define FRAME_LAG 2

/*
 * structure to track all objects related to a texture.
 */
struct texture_object {
  VkSampler sampler;

  VkImage image;
  VkImageLayout imageLayout;

  VkMemoryAllocateInfo mem_alloc;
  VkDeviceMemory mem;
  VkImageView view;
  uint32_t tex_width, tex_height;
};

typedef struct {
  VkImage image;
  VkCommandBuffer cmd;
  VkCommandBuffer graphics_to_present_cmd;
  VkImageView view;
} SwapchainBuffers;

#if defined(VK_USE_PLATFORM_MAGMA_KHR) && defined(CUBE_USE_IMAGE_PIPE)
struct FuchsiaState {
  async::Loop loop;
  uint32_t image_pipe_handle = 0;
  fuchsia::images::ImagePipePtr pipe;
  std::unique_ptr<mozart::ViewProviderService> view_provider_service;
  uint32_t num_frames = 60;
  uint32_t elapsed_frames = 0;
  std::chrono::time_point<std::chrono::high_resolution_clock> t0{};

  FuchsiaState() : loop(&kAsyncLoopConfigAttachToThread) {}
};
#endif

struct demo {
#if defined(VK_USE_PLATFORM_XLIB_KHR) | defined(VK_USE_PLATFORM_XCB_KHR)
  Display* display;
  Window xlib_window;
  Atom xlib_wm_delete_window;

  xcb_connection_t* connection;
  xcb_screen_t* screen;
  xcb_window_t xcb_window;
  xcb_intern_atom_reply_t* atom_wm_delete_window;
#elif defined(VK_USE_PLATFORM_MAGMA_KHR) && defined(CUBE_USE_IMAGE_PIPE)
  std::unique_ptr<FuchsiaState> fuchsia_state;
#endif

  VkSurfaceKHR surface;
  bool prepared;
  bool use_staging_buffer;
  bool use_xlib;
  bool separate_present_queue;

  VkInstance inst;
  VkPhysicalDevice gpu;
  VkDevice device;
  VkQueue graphics_queue;
  VkQueue present_queue;
  uint32_t graphics_queue_family_index;
  uint32_t present_queue_family_index;
  VkSemaphore image_acquired_semaphores[FRAME_LAG];
  VkSemaphore draw_complete_semaphores[FRAME_LAG];
  VkSemaphore image_ownership_semaphores[FRAME_LAG];
  VkPhysicalDeviceProperties gpu_props;
  VkQueueFamilyProperties* queue_props;
  VkPhysicalDeviceMemoryProperties memory_properties;

  uint32_t enabled_extension_count;
  uint32_t enabled_layer_count;
  const char* extension_names[64];
  const char* enabled_layers[64];

  uint32_t width, height;
  VkFormat format;
  VkColorSpaceKHR color_space;

  PFN_vkGetPhysicalDeviceSurfaceSupportKHR fpGetPhysicalDeviceSurfaceSupportKHR;
  PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR
      fpGetPhysicalDeviceSurfaceCapabilitiesKHR;
  PFN_vkGetPhysicalDeviceSurfaceFormatsKHR fpGetPhysicalDeviceSurfaceFormatsKHR;
  PFN_vkGetPhysicalDeviceSurfacePresentModesKHR
      fpGetPhysicalDeviceSurfacePresentModesKHR;
  PFN_vkGetPhysicalDeviceFeatures2KHR fpGetPhysicalDeviceFeatures2KHR;
  PFN_vkCreateSwapchainKHR fpCreateSwapchainKHR;
  PFN_vkDestroySwapchainKHR fpDestroySwapchainKHR;
  PFN_vkGetSwapchainImagesKHR fpGetSwapchainImagesKHR;
  PFN_vkAcquireNextImageKHR fpAcquireNextImageKHR;
  PFN_vkQueuePresentKHR fpQueuePresentKHR;
  PFN_vkCreateSamplerYcbcrConversionKHR fpCreateSamplerYcbcrConversionKHR;
  uint32_t swapchainImageCount;
  VkSwapchainKHR swapchain;
  SwapchainBuffers* buffers;
  VkPresentModeKHR presentMode;
  VkFence fences[FRAME_LAG];
  int frame_index;

  VkCommandPool cmd_pool;
  VkCommandPool present_cmd_pool;

  struct {
    VkFormat format;

    VkImage image;
    VkMemoryAllocateInfo mem_alloc;
    VkDeviceMemory mem;
    VkImageView view;
  } depth;

  struct texture_object textures[DEMO_TEXTURE_COUNT];
  struct texture_object staging_texture;

  struct {
    VkBuffer buf;
    VkMemoryAllocateInfo mem_alloc;
    VkDeviceMemory mem;
    VkDescriptorBufferInfo buffer_info;
  } uniform_data;

  VkCommandBuffer cmd;  // Buffer for initialization commands
  VkPipelineLayout pipeline_layout;
  VkDescriptorSetLayout desc_layout;
  VkPipelineCache pipelineCache;
  VkRenderPass render_pass;
  VkPipeline pipeline;

  mat4x4 projection_matrix;
  mat4x4 view_matrix;
  mat4x4 model_matrix;

  float spin_angle;
  float spin_increment;
  bool pause;

  VkShaderModule vert_shader_module;
  VkShaderModule frag_shader_module;

  VkDescriptorPool desc_pool;
  VkDescriptorSet desc_set;

  VkFramebuffer* framebuffers;

  bool quit;
  int32_t curFrame;
  int32_t frameCount;
  bool validate;
  bool use_break;
  bool suppress_popups;
  PFN_vkCreateDebugReportCallbackEXT CreateDebugReportCallback;
  PFN_vkDestroyDebugReportCallbackEXT DestroyDebugReportCallback;
  VkDebugReportCallbackEXT msg_callback;
  PFN_vkDebugReportMessageEXT DebugReportMessage;

  uint32_t current_buffer;
  uint32_t queue_family_count;
};

int test_vk_cube(int argc, char** argv);

void demo_init_vk_swapchain(struct demo* demo);

void demo_prepare(struct demo* demo);

void demo_update_data_buffer(struct demo* demo);

void demo_draw(struct demo* demo);

void demo_init(struct demo* demo, int argc, char** argv);

void demo_cleanup(struct demo* demo);
