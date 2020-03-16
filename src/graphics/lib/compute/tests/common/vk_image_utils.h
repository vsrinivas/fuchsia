// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_VK_IMAGE_UTILS_H_
#define SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_VK_IMAGE_UTILS_H_

#include <stdbool.h>
#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif

// Add a command to |command_buffer| to change the image layout of |image|
// from |src_layout| to |dst_layout|. |src_stage| and |dst_stage| describe
// the source and destination pipeline stages affected by the transition
// and will be used to compute the source and destination access flags
// automatically.
//
// NOTE: At the moment, only the following source stages are supported:
//       TOP_OF_PIPE, TRANSFER, COMPUTE_SHADER and COLOR_ATTACHMENT_OUTPUT.
//
// NOTE: At the moment, only the following destination stages are supported:
//       BOTTOM_OF_PIPE, TRANSFER, COMPUTE_SHADER
//
extern void
vk_cmd_image_layout_transition(VkCommandBuffer      command_buffer,
                               VkImage              image,
                               VkPipelineStageFlags src_stage,
                               VkImageLayout        src_layout,
                               VkPipelineStageFlags dst_stage,
                               VkImageLayout        dst_layout);

// Helper struct describing a {buffer,image} -> {buffer,image} copy
// operation's coordinate parameters.
// |src| describes the source {buffer,image}.
// |dst| describes the destination {buffer,image}
// |copy| describes the source and destination regions of the copy.
//
typedef struct vk_image_copy_info
{
  struct
  {
    uint32_t width;
    uint32_t height;
  } src;
  struct
  {
    uint32_t width;
    uint32_t height;
  } dst;
  struct
  {
    int32_t src_x;
    int32_t src_y;
    int32_t dst_x;
    int32_t dst_y;
    int32_t w;
    int32_t h;
  } copy;
} vk_image_copy_info_t;

// Adjust the |info->copy| fields to ensure that the copy operation only
// touches valid pixels in both the source and the destination. Return true
// if the operation requires copying at least one pixel, or false if it was
// completely clipped-out.
extern bool
vk_image_copy_info_clip(vk_image_copy_info_t * info);

// Add a command to |command_buffer| to copy a rectangle from
// |src_buffer| to |dst_image|. |src_stride| and |src_bytes_per_pixel|
// describe the pixel layout in the source buffer, and must match the
// image's format. |dst_image_layout| is the destination image's layout,
// and |info| describes the rectangle to copy.
extern void
vk_cmd_copy_buffer_to_image(VkCommandBuffer      command_buffer,
                            VkBuffer             src_buffer,
                            uint32_t             src_stride,
                            uint32_t             src_bytes_per_pixel,
                            VkImage              dst_image,
                            VkImageLayout        dst_image_layout,
                            vk_image_copy_info_t info);

// Add a command to |command_buffer| to copy a rectangle from
// |src_image| to |dst_image|. |src_image_layout| and |dst_image_layout| are
// the layout of the source and destination images, respectively, and |info|
// describes the rectangle to copy.
extern void
vk_cmd_copy_image_to_image(VkCommandBuffer      command_buffer,
                           VkImage              src_image,
                           VkImageLayout        src_image_layout,
                           VkImage              dst_image,
                           VkImageLayout        dst_image_layout,
                           vk_image_copy_info_t info);

// Add a command to |command_buffer| to copy a rectangle from |src_image|
// to |dst_buffer|.
extern void
vk_cmd_copy_image_to_buffer(VkCommandBuffer      command_buffer,
                            VkImage              src_image,
                            VkImageLayout        src_image_layout,
                            VkBuffer             dst_buffer,
                            uint32_t             dst_stride,
                            uint32_t             dst_bytes_per_pixel,
                            vk_image_copy_info_t info);

// Add a command to |command_buffer| to copy a rectangle from |src_buffer|
// to |dst_buffer|. Both buffers must have the same stride and pixel format.
extern void
vk_cmd_copy_buffer_to_buffer(VkCommandBuffer      command_buffer,
                             VkBuffer             src_buffer,
                             uint32_t             src_stride,
                             VkBuffer             dst_buffer,
                             uint32_t             dst_stride,
                             uint32_t             common_bytes_per_pixel,
                             vk_image_copy_info_t info);

#ifdef __cplusplus
}
#endif

#endif  // SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_VK_IMAGE_UTILS_H_
