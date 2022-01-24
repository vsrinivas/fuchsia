// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_INCLUDE_SPINEL_PLATFORMS_VK_SPINEL_VK_TYPES_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_INCLUDE_SPINEL_PLATFORMS_VK_SPINEL_VK_TYPES_H_

//
//
//

#include <vulkan/vulkan_core.h>

#include "spinel/spinel_types.h"

//
//
//

#ifdef __cplusplus
extern "C" {
#endif

//
// Spinel Vulkan target read-only opaque type
//
typedef struct spinel_vk_target const spinel_vk_target_t;

//
// Spinel Vulkan target requiremets
//
typedef struct spinel_vk_target_requirements
{
  uint32_t                           ext_name_count;
  char const **                      ext_names;
  VkPhysicalDeviceFeatures *         pdf;
  VkPhysicalDeviceVulkan11Features * pdf11;
  VkPhysicalDeviceVulkan12Features * pdf12;
} spinel_vk_target_requirements_t;

//
// Spinel Vulkan queue creation info
//
// The queue family must be compute-capable.
//
typedef struct spinel_vk_context_create_info_vk_queue
{
  VkDeviceQueueCreateFlags flags;
  uint32_t                 family_index;
  uint32_t                 count;
} spinel_vk_context_create_info_vk_queue_t;

//
// Spinel Vulkan queue family indices for shared resources.
//
#define SPN_VK_CONTEXT_CREATE_INFO_VK_QUEUE_SHARED_MAX_FAMILIES 2

typedef struct spinel_vk_context_create_info_vk_queue_shared
{
  uint32_t queue_family_count;
  uint32_t queue_family_indices[SPN_VK_CONTEXT_CREATE_INFO_VK_QUEUE_SHARED_MAX_FAMILIES];
} spinel_vk_context_create_info_vk_queue_shared_t;

//
// Spinel Vulkan environment
//
typedef struct spinel_vk_context_create_info_vk
{
  VkPhysicalDevice              pd;
  VkDevice                      d;
  VkPipelineCache               pc;
  VkAllocationCallbacks const * ac;
  struct
  {
    spinel_vk_context_create_info_vk_queue_t        compute;
    spinel_vk_context_create_info_vk_queue_shared_t shared;
  } q;
} spinel_vk_context_create_info_vk_t;

//
// Spinel Vulkan context creation
//
typedef struct spinel_vk_context_create_info
{
  spinel_vk_context_create_info_vk_t vk;               // Vulkan environment
  spinel_vk_target_t const *         target;           // Device-specific configuration data
  uint64_t                           block_pool_size;  // Block pool size in bytes
  uint32_t                           handle_count;     // Total handle count
} spinel_vk_context_create_info_t;

//
// Vulkan render extensions
//
// Possible rendering use cases supported by these extensions include:
//
//  1) Render and then copy the results to a debug buffer.
//  2) Render and then copy all altered tiles to an image.
//
// These buffer rendering extensions can be chained in any order but will always
// be executed in listed order on the queues.
//
// The compute extensions are submitted to the Spinel-managed compute queue.
//
// Optional graphics extensions are submitted to the provided graphics-capable
// queue.
//
//  COMPUTE QUEUE:
//
//   * COMPUTE_WAIT    : Wait before executing compute queue submission.
//   * COMPUTE_ACQUIRE : Acquire swapchain resources back from a queue family.
//   * COMPUTE_FILL    : Fill buffer.  This is a convenience extension.
//   * COMPUTE_RENDER  : Render tiles to a Spinel-managed surface.
//   * COMPUTE_COPY    : Copy to buffer for debugging.
//   * COMPUTE_RELEASE : Release swapchain resources to another queue family.
//   * COMPUTE_SIGNAL  : Signal compute queue submission is complete.
//
//  GRAPHICS QUEUE:
//
//   * GRAPHICS_WAIT   : Wait before executing the graphics queue submission.
//   * GRAPHICS_CLEAR  : Clear the image before storing altered tiles.
//   * GRAPHICS_STORE  : Store altered tiles to an image.
//   * GRAPHICS_SIGNAL : Signal submission is complete.
//
// TODO(allanmac):
//
//  * Copy changed buffer tiles to an image via a vertex+fragment pass in
//    order to benefit from fast clear and frame buffer compression.
//
typedef enum spinel_vk_swapchain_submit_ext_type_e
{
  SPN_VK_SWAPCHAIN_SUBMIT_EXT_TYPE_COMPUTE_WAIT,
  SPN_VK_SWAPCHAIN_SUBMIT_EXT_TYPE_COMPUTE_ACQUIRE,
  SPN_VK_SWAPCHAIN_SUBMIT_EXT_TYPE_COMPUTE_FILL,
  SPN_VK_SWAPCHAIN_SUBMIT_EXT_TYPE_COMPUTE_RENDER,
  SPN_VK_SWAPCHAIN_SUBMIT_EXT_TYPE_COMPUTE_COPY,
  SPN_VK_SWAPCHAIN_SUBMIT_EXT_TYPE_COMPUTE_RELEASE,
  SPN_VK_SWAPCHAIN_SUBMIT_EXT_TYPE_COMPUTE_SIGNAL,

  SPN_VK_SWAPCHAIN_SUBMIT_EXT_TYPE_GRAPHICS_WAIT,
  SPN_VK_SWAPCHAIN_SUBMIT_EXT_TYPE_GRAPHICS_CLEAR,
  SPN_VK_SWAPCHAIN_SUBMIT_EXT_TYPE_GRAPHICS_STORE,
  SPN_VK_SWAPCHAIN_SUBMIT_EXT_TYPE_GRAPHICS_SIGNAL,

  SPN_VK_SWAPCHAIN_SUBMIT_EXT_COUNT

} spinel_vk_swapchain_submit_ext_type_e;

//
// BASE extension simplifies walking a chain of extensions.
//
typedef struct spinel_vk_swapchain_submit_ext_base
{
  void *                                ext;
  spinel_vk_swapchain_submit_ext_type_e type;
} spinel_vk_swapchain_submit_ext_base_t;

//
// Imported semaphore waits
//
// Note that binary semaphores ignore associated values.
//
#define SPN_VK_SEMAPHORE_IMPORT_WAIT_SIZE 1

typedef struct spinel_vk_semaphore_import_wait
{
  // clang-format off
  uint32_t             count;
  VkPipelineStageFlags stages    [SPN_VK_SEMAPHORE_IMPORT_WAIT_SIZE];
  VkSemaphore          semaphores[SPN_VK_SEMAPHORE_IMPORT_WAIT_SIZE];
  uint64_t             values    [SPN_VK_SEMAPHORE_IMPORT_WAIT_SIZE];
  // clang-format on
} spinel_vk_semaphore_import_wait_t;

//
// Imported semaphore signals
//
// Note that binary semaphores ignore associated values.
//
#define SPN_VK_SEMAPHORE_IMPORT_SIGNAL_SIZE 2

typedef struct spinel_vk_semaphore_import_signal
{
  // clang-format off
  uint32_t    count;
  VkSemaphore semaphores[SPN_VK_SEMAPHORE_IMPORT_SIGNAL_SIZE];
  uint64_t    values    [SPN_VK_SEMAPHORE_IMPORT_SIGNAL_SIZE];
  // clang-format on
} spinel_vk_semaphore_import_signal_t;

//
// COMPUTE WAIT
//
typedef struct spinel_vk_swapchain_submit_ext_compute_wait
{
  void *                                ext;
  spinel_vk_swapchain_submit_ext_type_e type;
  spinel_vk_semaphore_import_wait_t     wait;
} spinel_vk_swapchain_submit_ext_compute_wait_t;

//
// COMPUTE ACQUIRE
//
// Only necessary if Spinel swapchain storage buffer was created with
// VK_SHARING_MODE_EXCLUSIVE.
//
typedef struct spinel_vk_swapchain_submit_ext_compute_acquire
{
  void *                                ext;
  spinel_vk_swapchain_submit_ext_type_e type;
  uint32_t                              from_queue_family_index;
} spinel_vk_swapchain_submit_ext_compute_acquire_t;

//
// COMPUTE FILL
//
typedef struct spinel_vk_swapchain_submit_ext_compute_fill
{
  void *                                ext;
  spinel_vk_swapchain_submit_ext_type_e type;
  uint32_t                              dword;
} spinel_vk_swapchain_submit_ext_compute_fill_t;

//
// COMPUTE RENDER
//
//  - The clip is in pixels.
//  - Requires (x0<=x1) and (y0<=y1).
//  - Clip is dilated to tile boundaries.
//
typedef struct spinel_vk_swapchain_submit_ext_compute_render
{
  void *                                ext;
  spinel_vk_swapchain_submit_ext_type_e type;
  spinel_pixel_clip_t                   clip;
  uint32_t                              extent_index;
} spinel_vk_swapchain_submit_ext_compute_render_t;

//
// COMPUTE COPY
//
// Requirements:
//
//  * `.dst.buffer` created with:
//
//    - VK_BUFFER_USAGE_TRANSFER_DST_BIT
//
//  * `.dst.range` is the number of bytes copied.
//
typedef struct spinel_vk_swapchain_submit_ext_compute_copy
{
  void *                                ext;
  spinel_vk_swapchain_submit_ext_type_e type;
  VkDescriptorBufferInfo                dst;
} spinel_vk_swapchain_submit_ext_compute_copy_t;

//
// COMPUTE RELEASE
//
// Only necessary if Spinel swapchain storage buffer was created with
// VK_SHARING_MODE_EXCLUSIVE.
//
typedef struct spinel_vk_swapchain_submit_ext_compute_release
{
  void *                                ext;
  spinel_vk_swapchain_submit_ext_type_e type;
  uint32_t                              to_queue_family_index;
} spinel_vk_swapchain_submit_ext_compute_release_t;

//
// COMPUTE SIGNAL
//
typedef struct spinel_vk_swapchain_submit_ext_compute_signal
{
  void *                                ext;
  spinel_vk_swapchain_submit_ext_type_e type;
  spinel_vk_semaphore_import_signal_t   signal;
} spinel_vk_swapchain_submit_ext_compute_signal_t;

//
// GRAPHICS WAIT
//
// Binary or timeline semaphore signaled when the image is available.
//
typedef struct spinel_vk_swapchain_submit_ext_graphics_wait
{
  void *                                ext;
  spinel_vk_swapchain_submit_ext_type_e type;
  spinel_vk_semaphore_import_wait_t     wait;
} spinel_vk_swapchain_submit_ext_graphics_wait_t;

//
// GRAPHICS CLEAR
//
// Fast clears `graphics_store.image` before storing changed swapchain tiles to
// the image.
//
typedef struct spinel_vk_swapchain_submit_ext_graphics_clear
{
  void *                                ext;
  spinel_vk_swapchain_submit_ext_type_e type;
  VkClearColorValue                     color;
} spinel_vk_swapchain_submit_ext_graphics_clear_t;

//
// GRAPHICS STORE
//
// Stores changed swapchain tiles to `.image`.
//
// Necessary queue ownership transfers and layout transisitions are implicitly
// handled.
//
typedef struct spinel_vk_swapchain_submit_ext_graphics_store
{
  void *                                ext;
  spinel_vk_swapchain_submit_ext_type_e type;
  uint32_t                              extent_index;
  VkCommandBuffer                       cb;
  VkQueue                               queue;
  uint32_t                              queue_family_index;
  VkImageLayout                         old_layout;
  VkImage                               image;
  VkDescriptorImageInfo                 image_info;
} spinel_vk_swapchain_submit_ext_graphics_store_t;

//
// GRAPHICS SIGNAL
//
// Binary or timeline semaphore signaled when the image is presentable.
//
typedef struct spinel_vk_swapchain_submit_ext_graphics_signal
{
  void *                                ext;
  spinel_vk_swapchain_submit_ext_type_e type;
  spinel_vk_semaphore_import_signal_t   signal;
} spinel_vk_swapchain_submit_ext_graphics_signal_t;

//
//
//

#ifdef __cplusplus
}
#endif

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_INCLUDE_SPINEL_PLATFORMS_VK_SPINEL_VK_TYPES_H_
