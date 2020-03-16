// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tests/common/vk_image_utils.h"

#include "tests/common/utils.h"

void
vk_cmd_image_layout_transition(VkCommandBuffer      command_buffer,
                               VkImage              image,
                               VkPipelineStageFlags src_stage,
                               VkImageLayout        src_layout,
                               VkPipelineStageFlags dst_stage,
                               VkImageLayout        dst_layout)
{
  // TODO(digit): Complete this and put in test framework header.
  VkAccessFlags src_access = (VkAccessFlags)0;
  switch (src_stage)
    {
      case VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT:
        break;
      case VK_PIPELINE_STAGE_TRANSFER_BIT:
        src_access = VK_ACCESS_TRANSFER_READ_BIT;
        break;
      case VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT:
        src_access = VK_ACCESS_SHADER_READ_BIT;
        break;
      case VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT:
        src_access = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        break;
      default:
        ASSERT_MSG(false, "Unsupported source pipeline stage 0x%x\n", src_stage);
    }

  // TODO(digit): Complete this and put in test framework header.
  VkAccessFlags dst_access = (VkAccessFlags)0;
  switch (dst_stage)
    {
      case VK_PIPELINE_STAGE_TRANSFER_BIT:
        dst_access = VK_ACCESS_TRANSFER_WRITE_BIT;
        break;
      case VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT:
        dst_access = VK_ACCESS_SHADER_WRITE_BIT;
        break;
      case VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT:
        break;
      default:
        ASSERT_MSG(false, "Unsupported destination pipeline stage 0x%x\n", src_stage);
    }

  const VkImageMemoryBarrier image_memory_barrier = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .pNext = NULL,
      .srcAccessMask = src_access,
      .dstAccessMask = dst_access,
      .oldLayout = src_layout,
      .newLayout = dst_layout,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image = image,
      .subresourceRange = {
          .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
          .baseMipLevel   = 0,
          .levelCount     = 1,
          .baseArrayLayer = 0,
          .layerCount     = 1,
      },
  };

  vkCmdPipelineBarrier(command_buffer,
                       src_stage,
                       dst_stage,
                       0,     // dependencyFlags
                       0,     // memoryBarrierCount
                       NULL,  // pMemoryBarriers
                       0,     // bufferMemoryBarrierCount
                       NULL,  // pBufferMemoryBarriers
                       1,     // imageMemoryBarrierCount,
                       &image_memory_barrier);
}

bool
vk_image_copy_info_clip(vk_image_copy_info_t * info)
{
  if (info->copy.src_x < 0)
    {
      info->copy.w += info->copy.src_x;
      info->copy.dst_x -= info->copy.src_x;
      info->copy.src_x = 0;
    }
  if (info->copy.src_y < 0)
    {
      info->copy.h += info->copy.src_y;
      info->copy.dst_y -= info->copy.src_y;
      info->copy.src_y = 0;
    }

  if (info->copy.dst_x < 0)
    {
      info->copy.w += info->copy.dst_x;
      info->copy.src_x -= info->copy.dst_x;
      info->copy.dst_x = 0;
    }
  if (info->copy.dst_y < 0)
    {
      info->copy.h += info->copy.dst_y;
      info->copy.src_y -= info->copy.dst_y;
      info->copy.dst_y = 0;
    }

  int32_t delta;

  delta = info->copy.src_x + info->copy.w - (int32_t)info->src.width;
  if (delta > 0)
    {
      info->copy.w -= delta;
    }
  delta = info->copy.src_y + info->copy.h - (int32_t)info->src.height;
  if (delta > 0)
    {
      info->copy.h -= delta;
    }
  delta = info->copy.dst_x + info->copy.w - (int32_t)info->dst.width;
  if (delta > 0)
    {
      info->copy.w -= delta;
    }
  delta = info->copy.dst_y + info->copy.h - (int32_t)info->dst.height;
  if (delta > 0)
    {
      info->copy.h -= delta;
    }

  return (info->copy.w > 0 && info->copy.h > 0);
}

extern void
vk_cmd_copy_buffer_to_image(VkCommandBuffer      command_buffer,
                            VkBuffer             src_buffer,
                            uint32_t             src_stride,
                            uint32_t             src_bytes_per_pixel,
                            VkImage              dst_image,
                            VkImageLayout        dst_image_layout,
                            vk_image_copy_info_t info)
{
  if (!vk_image_copy_info_clip(&info))
    return;

  const VkBufferImageCopy buffer_image_copy = {
      .bufferOffset      = (VkDeviceSize)(info.copy.src_y * src_stride +
                                          info.copy.src_x * src_bytes_per_pixel),
      .bufferRowLength   = src_stride / src_bytes_per_pixel,
      .bufferImageHeight = (uint32_t) info.copy.h,
      .imageSubresource = {
        .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
        .mipLevel       = 0,
        .baseArrayLayer = 0,
        .layerCount     = 1,
      },
      .imageOffset = {
        .x = info.copy.dst_x,
        .y = info.copy.dst_y,
        .z = 0,
      },
      .imageExtent = {
        .width  = (uint32_t) info.copy.w,
        .height = (uint32_t) info.copy.h,
        .depth  = 1,
      },
  };

  vkCmdCopyBufferToImage(command_buffer,
                         src_buffer,
                         dst_image,
                         dst_image_layout,
                         1,
                         &buffer_image_copy);
}

extern void
vk_cmd_copy_image_to_image(VkCommandBuffer      command_buffer,
                           VkImage              src_image,
                           VkImageLayout        src_image_layout,
                           VkImage              dst_image,
                           VkImageLayout        dst_image_layout,
                           vk_image_copy_info_t info)
{
  if (!vk_image_copy_info_clip(&info))
    return;

  const VkImageCopy image_copy = {
      .srcSubresource = {
        .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
        .mipLevel       = 0,
        .baseArrayLayer = 0,
        .layerCount     = 1,
      },
      .srcOffset = {
        .x = info.copy.src_x,
        .y = info.copy.src_y,
        .z = 0,
      },

      .dstSubresource = {
        .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
        .mipLevel       = 0,
        .baseArrayLayer = 0,
        .layerCount     = 1,
      },
      .dstOffset = {
        .x = info.copy.dst_x,
        .y = info.copy.dst_y,
        .z = 0,
      },

      .extent = {
        .width  = (uint32_t) info.copy.w,
        .height = (uint32_t) info.copy.h,
        .depth  = 1,
      },
  };

  vkCmdCopyImage(command_buffer,
                 src_image,
                 src_image_layout,
                 dst_image,
                 dst_image_layout,
                 1,
                 &image_copy);
}

extern void
vk_cmd_copy_image_to_buffer(VkCommandBuffer      command_buffer,
                            VkImage              src_image,
                            VkImageLayout        src_image_layout,
                            VkBuffer             dst_buffer,
                            uint32_t             dst_stride,
                            uint32_t             dst_bytes_per_pixel,
                            vk_image_copy_info_t info)
{
  if (!vk_image_copy_info_clip(&info))
    return;

  const VkBufferImageCopy buffer_image_copy = {
      .bufferOffset      = (VkDeviceSize)(info.copy.dst_y * dst_stride +
                                          info.copy.dst_x * dst_bytes_per_pixel),
      .bufferRowLength   = dst_stride / dst_bytes_per_pixel,
      .bufferImageHeight = (uint32_t) info.copy.h,
      .imageSubresource = {
        .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
        .mipLevel       = 0,
        .baseArrayLayer = 0,
        .layerCount     = 1,
      },
      .imageOffset = {
        .x = info.copy.src_x,
        .y = info.copy.src_y,
        .z = 0,
      },
      .imageExtent = {
        .width  = (uint32_t) info.copy.w,
        .height = (uint32_t) info.copy.h,
        .depth  = 1,
      },
  };

  vkCmdCopyImageToBuffer(command_buffer,
                         src_image,
                         src_image_layout,
                         dst_buffer,
                         1,
                         &buffer_image_copy);
}

extern void
vk_cmd_copy_buffer_to_buffer(VkCommandBuffer      command_buffer,
                             VkBuffer             src_buffer,
                             uint32_t             src_stride,
                             VkBuffer             dst_buffer,
                             uint32_t             dst_stride,
                             uint32_t             common_bytes_per_pixel,
                             vk_image_copy_info_t info)
{
  if (!vk_image_copy_info_clip(&info))
    return;

  // Perform a single copy operation when possible.
  if (src_stride == dst_stride && info.copy.w * common_bytes_per_pixel == src_stride)
    {
      vkCmdCopyBuffer(
        command_buffer,
        src_buffer,
        dst_buffer,
        1,
        &(const VkBufferCopy){
          .srcOffset = info.copy.src_x * common_bytes_per_pixel + info.copy.src_y * src_stride,
          .dstOffset = info.copy.dst_x * common_bytes_per_pixel + info.copy.dst_y * dst_stride,
          .size      = src_stride * info.copy.h,
        });
      return;
    }

    // Otherwise, enqueue multiple commands, where each command tries to copy
    // up to 16 scanlines.
#define MAX_SCANLINES 16u
  VkBufferCopy copies[MAX_SCANLINES];
  uint32_t     count = 0;
  for (int32_t y = 0; y < info.copy.h; y++)
    {
      copies[count] = (const VkBufferCopy){
        .srcOffset = info.copy.src_x * common_bytes_per_pixel + (info.copy.src_y + y) * src_stride,
        .dstOffset = info.copy.dst_x * common_bytes_per_pixel + (info.copy.dst_y + y) * dst_stride,
        .size      = common_bytes_per_pixel * info.copy.w,
      };

      count++;
      if (count == MAX_SCANLINES || y + (int32_t)count == info.copy.h)
        {
          vkCmdCopyBuffer(command_buffer, src_buffer, dst_buffer, count, copies);
          count = 0;
        }
    }
}
