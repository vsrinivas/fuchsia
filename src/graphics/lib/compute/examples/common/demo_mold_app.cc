// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "demo_mold_app.h"

#include <stdio.h>

#include "mold/mold.h"
#include "spinel/spinel.h"
#include "spinel/spinel_assert.h"
#include "tests/common/utils.h"
#include "tests/common/vk_utils.h"

// Set to 1 to enable log messages during development.
#define ENABLE_LOG 0

#if ENABLE_LOG
#define LOG(...) fprintf(stderr, __VA_ARGS__)
#else
#define LOG(...) ((void)0)
#endif

//
//
//

// Expected by the spn() macro that is used by demo1_state.h indirectly.
spn_result_t
spn_assert_1(const char * file, int line, bool fatal, spn_result_t result)
{
  if (result != SPN_SUCCESS)
    {
      fprintf(stderr, "%s:%d: spinel failure %d\n", file, line, result);
      abort();
    }
  return result;
}

// Not implemented by Mold yet!!
spn_result_t
spn_composition_set_clip(spn_composition_t composition, uint32_t const clip[4])
{
  return SPN_SUCCESS;
}

// Not implemented by Mold yet!!
extern "C" spn_result_t
spn_path_builder_ellipse(spn_path_builder_t, float, float, float, float)
{
  return SPN_SUCCESS;
}

#define SPN_DEMO_SURFACE_CHANNEL_TYPE uint32_t
#define SPN_DEMO_SURFACE_WIDTH 1024
#define SPN_DEMO_SURFACE_HEIGHT 1024
#define SPN_DEMO_SURFACE_PIXELS (SPN_DEMO_SURFACE_WIDTH * SPN_DEMO_SURFACE_HEIGHT)
#define SPN_DEMO_SURFACE_SIZE (SPN_DEMO_SURFACE_PIXELS * 4 * sizeof(SPN_DEMO_SURFACE_CHANNEL_TYPE))

//
//
//

// Helper struct describing a {buffer,image} -> {buffer,image} copy
// operation's coordinate parameters.
// |src| describes the source {buffer,image}.
// |dst| describes the destination {buffer,image}
// |copy| describes the source and destination regions of the copy.
//
typedef struct
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
} CopyInfo;

static bool
copy_info_compute_clip(CopyInfo * info)
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
  delta = info->copy.dst_y + info->copy.h - (int32_t)info->dst.width;
  if (delta > 0)
    {
    }

  return (info->copy.w > 0 && info->copy.h > 0);
}

static void
cmdCopyBufferToImage(VkCommandBuffer command_buffer,
                     VkBuffer        src_buffer,
                     uint32_t        src_stride,
                     uint32_t        src_bytes_per_pixel,
                     VkImage         dst_image,
                     VkImageLayout   dst_image_layout,
                     CopyInfo        info)
{
  if (!copy_info_compute_clip(&info))
    return;

  const VkBufferImageCopy buffer_image_copy = {
      .bufferOffset      = (VkDeviceSize)(info.copy.src_y * src_stride +
                                          info.copy.src_x * src_bytes_per_pixel),
      .bufferRowLength   = src_stride,
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

static void
cmdImageLayoutTransition(VkCommandBuffer      command_buffer,
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

//
//
//

//
//
//

DemoMoldApp::DemoMoldApp(const Config & config) : config_no_clear_(config.no_clear)
{
  DemoVulkanApp::Config app_config  = config.app;
  app_config.enable_swapchain_queue = true;
  this->DemoVulkanApp::init(app_config);
  mold_context_create(&spinel_context_);
}

DemoMoldApp::~DemoMoldApp()
{
  spn_context_release(spinel_context_);
}

//
//
//
bool
DemoMoldApp::setup()
{
  LOG("SETUP\n");
  image_provider_->setup(spinel_context_,
                         swapchain_image_count_,
                         swapchain_extent_.width,
                         swapchain_extent_.height);

  for (uint32_t nn = 0; nn < swapchain_image_count_; ++nn)
    image_buffers_.push_back(ScopedBuffer(SPN_DEMO_SURFACE_SIZE,
                                          VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                          app_state_.pd,
                                          app_state_.d,
                                          app_state_.ac));

  // Prepare the command buffer for each swapchain image.
  for (uint32_t nn = 0; nn < swapchain_image_count_; ++nn)
    {
      const vk_swapchain_queue_image_t * image = vk_swapchain_queue_get_image(swapchain_queue_, nn);
      VkCommandBuffer                    buffer = image->command_buffer;

      const VkCommandBufferBeginInfo beginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT,
      };
      vk(BeginCommandBuffer(buffer, &beginInfo));

      // Step 1) transition image to TRANSFER_DST_OPTIMAL layout.
      cmdImageLayoutTransition(buffer,
                               image->image,
                               VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                               VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                               VK_PIPELINE_STAGE_TRANSFER_BIT,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

      // Step 2) copy buffer into image.
      cmdCopyBufferToImage(
      buffer,
      image_buffers_[nn]->buffer,
      SPN_DEMO_SURFACE_WIDTH * 4,
      4,
      image->image,
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      (const CopyInfo){
          .src = {
            .width  = SPN_DEMO_SURFACE_WIDTH,
            .height = SPN_DEMO_SURFACE_HEIGHT,
          },
          .dst = {
            .width  = swapchain_extent_.width,
            .height = swapchain_extent_.height,
          },
          .copy = {
            .src_x = 0,
            .src_y = 0,
            .dst_x = (int32_t)(swapchain_extent_.width - SPN_DEMO_SURFACE_WIDTH)/2,
            .dst_y = (int32_t)(swapchain_extent_.height - SPN_DEMO_SURFACE_HEIGHT)/2,
            .w     = SPN_DEMO_SURFACE_WIDTH,
            .h     = SPN_DEMO_SURFACE_HEIGHT,
          },
      });

      // Step 3) transition image back to PRESENT_SRC_KHR.
      cmdImageLayoutTransition(buffer,
                               image->image,
                               VK_PIPELINE_STAGE_TRANSFER_BIT,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                               VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

      vk(EndCommandBuffer(buffer));
    }

  LOG("SETUP COMPLETED\n");
  return true;
}

//
//
//
void
DemoMoldApp::teardown()
{
  LOG("TEARDOWN\n");
  // TODO(digit): Wait for Spinel context drain
  spn_context_status(spinel_context_);

  image_buffers_.clear();
  image_provider_->teardown();
  this->DemoVulkanApp::teardown();
  LOG("TEARDOWN COMPLETED\n");
}

//
//
//

bool
DemoMoldApp::drawFrame(uint32_t frame_counter)
{
  if (!acquireSwapchainQueueImage())
    return false;

  // Setup image.
  DemoSpinelImage & demo_image   = image_provider_->getImage(image_index_);
  ScopedBuffer &    image_buffer = image_buffers_[image_index_];

  LOG("FRAME %u\n", frame_counter);

  demo_image.setupPaths(frame_counter);
  demo_image.setupRasters(frame_counter);
  demo_image.setupLayers(frame_counter);

  // Render it to the buffer with Mold.
  LOG("FRAME RENDER\n");

  if (!config_no_clear_)
    memset(image_buffer->mapped, 0xff, image_buffer->size);

  mold_pixel_format_t pixel_format = MOLD_RGBA8888;
  if (swapchain_surface_format_.format == VK_FORMAT_B8G8R8A8_UNORM)
    pixel_format = MOLD_BGRA8888;

  mold_raw_buffer_t mold_target_buffer = {
    .buffer = image_buffer->mapped,
    .width  = SPN_DEMO_SURFACE_WIDTH * 4,
    .format = pixel_format,
  };

  demo_image.render(&mold_target_buffer, SPN_DEMO_SURFACE_WIDTH, SPN_DEMO_SURFACE_HEIGHT);

  vk_buffer_flush_all(&image_buffer);

  presentSwapchainQueueImage();

  LOG("FRAME SUBMITTED\n");

  demo_image.resetLayers();
  demo_image.resetRasters();
  demo_image.resetPaths();
  LOG("FRAME COMPLETED\n");
  return true;
}
