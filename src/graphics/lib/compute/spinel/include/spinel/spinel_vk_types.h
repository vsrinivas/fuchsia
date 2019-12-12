// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL_INCLUDE_SPINEL_SPINEL_VK_TYPES_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL_INCLUDE_SPINEL_SPINEL_VK_TYPES_H_

//
//
//

#include <vulkan/vulkan_core.h>

#include "spinel_types.h"

//
//
//

#ifdef __cplusplus
extern "C" {
#endif

//
// VULKAN TARGET REQUIREMENTS
//

struct spn_vk_target_requirements
{
  uint32_t                    qci_count;
  VkDeviceQueueCreateInfo *   qcis;
  uint32_t                    ext_name_count;
  char const **               ext_names;
  VkPhysicalDeviceFeatures2 * pdf2;
};

//
// VULKAN CONTEXT CREATION
//

struct spn_vk_environment
{
  VkDevice                         d;
  VkAllocationCallbacks const *    ac;
  VkPipelineCache                  pc;
  VkPhysicalDevice                 pd;
  VkPhysicalDeviceMemoryProperties pdmp;  // FIXME(allanmac): get rid of this member
  uint32_t                         qfi;   // FIXME(allanmac): get rid of this member
};

struct spn_vk_context_create_info
{
  //
  // NOTE(allanmac): This interface is in flux.
  //
  // When Spinel constructs a target for a particular device, it also
  // generates a custom HotSort target.  These will always be bundled
  // together.
  //
  struct spn_vk_target const *     spinel;
  struct hotsort_vk_target const * hotsort;
  uint64_t                         block_pool_size;
  uint32_t                         handle_count;
};

//
// VULKAN RENDER EXTENSIONS
//
//
// These extensions can be chained in any order but will always be
// executed in the following order:
//
//   PRE_BARRIER>PRE_CLEAR>PRE_PROCESS>RENDER>POST_PROCESS>POST_COPY>POST_BARRIER
//
// Note that this is the same order as the enum.
//
// The pre/post barriers are used to declare an image layout transition
// or a queue family ownership transfer.
//

typedef enum spn_vk_render_submit_ext_type_e
{
  SPN_VK_RENDER_SUBMIT_EXT_TYPE_IMAGE_PRE_BARRIER,
  SPN_VK_RENDER_SUBMIT_EXT_TYPE_IMAGE_PRE_CLEAR,
  SPN_VK_RENDER_SUBMIT_EXT_TYPE_IMAGE_PRE_PROCESS,
  SPN_VK_RENDER_SUBMIT_EXT_TYPE_IMAGE_RENDER,
  SPN_VK_RENDER_SUBMIT_EXT_TYPE_IMAGE_POST_PROCESS,
  SPN_VK_RENDER_SUBMIT_EXT_TYPE_IMAGE_POST_COPY_TO_BUFFER,
  SPN_VK_RENDER_SUBMIT_EXT_TYPE_IMAGE_POST_COPY_TO_IMAGE,
  SPN_VK_RENDER_SUBMIT_EXT_TYPE_IMAGE_POST_BARRIER
} spn_vk_render_submit_ext_type_e;

//
// RENDER TO AN IMAGE
//
// The callback submits an executable command buffer with the
// Spinel-managed queue and fence.
//
// The callback provides a Spinel client an opportunity to integrate
// with a swapchain or include application-specific semaphores and
// command buffers.
//
// The callback will be invoked after the spn_render() and before either
// of the associated composition or styling are unsealed.
//
// The callback is guaranteed to be invoked once.
//
// NOTE(allanmac): Use of a callback will be unnecessary once timeline
// semaphores are available and this interface will be replaced.
//
// FIXME(allanmac): We probably want to submit the layout transition
// immediately after acquiring the image and not include it in the
// executable command buffer submitted by the callback.
//
typedef void (*spn_vk_render_submit_ext_image_render_pfn_t)(VkQueue               queue,
                                                            VkFence               fence,
                                                            VkCommandBuffer const cb,
                                                            void *                data);

typedef struct spn_vk_render_submit_ext_image_render
{
  void *                                      ext;
  spn_vk_render_submit_ext_type_e             type;
  VkImage                                     image;
  VkDescriptorImageInfo                       image_info;
  spn_vk_render_submit_ext_image_render_pfn_t submitter_pfn;
  void *                                      submitter_data;
} spn_vk_render_submit_ext_image_render_t;

//
// PRE-RENDER IMAGE BARRIER
//

typedef struct spn_vk_render_submit_ext_image_pre_barrier
{
  void *                          ext;
  spn_vk_render_submit_ext_type_e type;
  VkImageLayout                   old_layout;
  uint32_t                        src_qfi;  // queue family index
} spn_vk_render_submit_ext_image_pre_barrier_t;

//
// PRE-RENDER IMAGE CLEAR
//

typedef struct spn_vk_render_submit_ext_image_pre_clear
{
  void *                          ext;
  spn_vk_render_submit_ext_type_e type;
  VkClearColorValue const *       color;
} spn_vk_render_submit_ext_image_pre_clear_t;

//
// PRE/POST-RENDER PROCESS
//

typedef struct spn_vk_render_submit_ext_image_process
{
  void *                          ext;
  spn_vk_render_submit_ext_type_e type;
  uint32_t                        access_mask;
  VkPipeline                      pipeline;
  VkPipelineLayout                pipeline_layout;
  uint32_t                        descriptor_set_count;
  const VkDescriptorSet *         descriptor_sets;
  uint32_t                        push_offset;
  uint32_t                        push_size;
  const void *                    push_values;
  uint32_t                        group_count_x;
  uint32_t                        group_count_y;
  uint32_t                        group_count_z;
} spn_vk_render_submit_ext_image_process_t;

//
// POST-RENDER IMAGE COPY TO A BUFFER
//

typedef struct spn_vk_render_submit_ext_image_post_copy_to_buffer
{
  void *                          ext;
  spn_vk_render_submit_ext_type_e type;
  VkBuffer                        dst;
  uint32_t                        region_count;
  const VkBufferImageCopy *       regions;
} spn_vk_render_submit_ext_image_post_copy_to_buffer_t;

//
// POST-RENDER IMAGE COPY TO AN IMAGE
//

typedef struct spn_vk_render_submit_ext_image_post_copy_to_image
{
  void *                          ext;
  spn_vk_render_submit_ext_type_e type;
  VkImage                         dst;
  VkImageLayout                   dst_layout;
  uint32_t                        region_count;
  const VkImageCopy *             regions;
} spn_vk_render_submit_ext_image_post_copy_to_image_t;

//
// POST-RENDER IMAGE BARRIER
//

typedef struct spn_vk_render_submit_ext_image_post_barrier
{
  void *                          ext;
  spn_vk_render_submit_ext_type_e type;
  VkImageLayout                   new_layout;
  uint32_t                        dst_qfi;  // queue family index
} spn_vk_render_submit_ext_image_post_barrier_t;

//
//
//

#ifdef __cplusplus
}
#endif

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL_INCLUDE_SPINEL_SPINEL_VK_TYPES_H_
