// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "skia_test_app.h"

#include <stdio.h>
#include <stdlib.h>

// tests/common includes.
#include "tests/common/utils.h"
#include "tests/common/vk_app_state.h"
#include "tests/common/vk_swapchain.h"
#include "tests/common/vk_utils.h"

// Skia includes.
#include "include/core/SkSurface.h"
#include "include/gpu/GrContext.h"
#include "include/gpu/vk/GrVkBackendContext.h"
#include "include/gpu/vk/GrVkExtensions.h"

//
//  Helper functions
//

static VkPipelineStageFlags
vk_image_layout_to_src_stage(const VkImageLayout layout)
{
  switch (layout)
    {
      case VK_IMAGE_LAYOUT_UNDEFINED:
        return VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
      case VK_IMAGE_LAYOUT_GENERAL:
        return VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
      case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
        return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
      case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
      case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
        return VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
      case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
        return VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
      case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
      case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
        return VK_PIPELINE_STAGE_TRANSFER_BIT;
      case VK_IMAGE_LAYOUT_PREINITIALIZED:
        return VK_PIPELINE_STAGE_HOST_BIT;
      case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
        return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
      default:
        ASSERT_MSG(false, "Unsupported image layout %u\n", layout);
        //return VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    }
}

static VkAccessFlags
vk_image_layout_to_src_access_mask(const VkImageLayout layout)
{
  switch (layout)
    {
      case VK_IMAGE_LAYOUT_UNDEFINED:
        return 0;  // No writes needed.
      case VK_IMAGE_LAYOUT_GENERAL:
        return VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
               VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT |
               VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_HOST_READ_BIT;
      case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
        return VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
      case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
        return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
      case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
        return 0;  // No writes needed.
      case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
        return 0;  // No writes needed.
      case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
        return VK_ACCESS_TRANSFER_WRITE_BIT;
        break;
      case VK_IMAGE_LAYOUT_PREINITIALIZED:
        return VK_ACCESS_HOST_WRITE_BIT;
        break;
      case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
        return 0;  // No writes needed.
      default:
        ASSERT_MSG(false, "Unsupported source image layout %u\n", layout);
        //return 0;
    }
}

static VkAccessFlags
vk_image_layout_to_dst_access_mask(const VkImageLayout layout)
{
  switch (layout)
    {
      case VK_IMAGE_LAYOUT_UNDEFINED:
        ASSERT_MSG(false, "VK_IMAGE_LAYOUT_UNDEFINED cannot be used for destination layouts!");
        return 0;
      case VK_IMAGE_LAYOUT_GENERAL:
        return VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
               VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT |
               VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_HOST_READ_BIT;
      case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
        return VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
      case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
        return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
      case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
      case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
        return VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
      case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
        return VK_ACCESS_TRANSFER_READ_BIT;
      case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
        return VK_ACCESS_TRANSFER_WRITE_BIT;
      case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
        return VK_ACCESS_MEMORY_READ_BIT;
      default:
        ASSERT_MSG(false, "Unsupported destination image layout %u\n", layout);
        return 0;
    }
}

// Record an image layout transition in |command_buffer| for |image|.
// NOTE: This computes the destination access mask direction from |new_layout|.
static void
image_layout_transition_generic(VkCommandBuffer      command_buffer,
                                VkImage              image,
                                VkImageLayout        old_layout,
                                VkImageLayout        new_layout,
                                VkPipelineStageFlags src_stage_mask,
                                VkAccessFlags        src_access_flags,
                                VkPipelineStageFlags dst_stage_mask,
                                VkAccessFlags        dst_access_flags)
{
  const VkImageMemoryBarrier imageBarrier = {
    .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
    .pNext = NULL,
    .srcAccessMask = src_access_flags,
    .dstAccessMask = dst_access_flags,
    .oldLayout = old_layout,
    .newLayout = new_layout,
    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
    .image = image,
    .subresourceRange = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1,
    },
  };

  vkCmdPipelineBarrier(command_buffer,
                       src_stage_mask,
                       dst_stage_mask,
                       0,               // dependencyFlags
                       0,               // memoryBarrierCount
                       NULL,            // pMemoryBarriers
                       0,               // bufferMemoryBarrierCount
                       NULL,            // pBufferMemoryBarries
                       1,               // imageMemoryBarrierCount,
                       &imageBarrier);  // pImageMemoryBarriers
}

static void
image_layout_transition(VkCommandBuffer command_buffer,
                        VkImage         image,
                        VkImageLayout   old_layout,
                        VkImageLayout   new_layout)
{
  VkPipelineStageFlags src_stage_mask  = vk_image_layout_to_src_stage(old_layout);
  VkAccessFlags        src_access_mask = vk_image_layout_to_src_access_mask(old_layout);
  VkPipelineStageFlags dst_stage_mask  = VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
  VkAccessFlags        dst_access_mask = vk_image_layout_to_dst_access_mask(new_layout);

  image_layout_transition_generic(command_buffer,
                                  image,
                                  old_layout,
                                  new_layout,
                                  src_stage_mask,
                                  src_access_mask,
                                  dst_stage_mask,
                                  dst_access_mask);
}

//
//
//

static void
command_buffer_begin(VkCommandBuffer command_buffer)
{
  const VkCommandBufferBeginInfo beginInfo = {
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    .flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT,
  };
  vk(BeginCommandBuffer(command_buffer, &beginInfo));
}

static void
command_buffer_submit(VkCommandBuffer command_buffer,
                      VkQueue         queue,
                      VkDevice        device,
                      VkSemaphore     wait_semaphore,
                      VkSemaphore     signal_semaphore,
                      VkFence         fence,
                      bool            wait_fence)
{
  const VkPipelineStageFlags waitStages[] = {
    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
  };

  uint32_t wait_count   = (wait_semaphore != VK_NULL_HANDLE) ? 1 : 0;
  uint32_t signal_count = (signal_semaphore != VK_NULL_HANDLE) ? 1 : 0;

  VkSubmitInfo submitInfo = {
    .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
    .waitSemaphoreCount   = wait_count,
    .pWaitSemaphores      = wait_count ? &wait_semaphore : VK_NULL_HANDLE,
    .pWaitDstStageMask    = wait_count ? waitStages : NULL,
    .commandBufferCount   = 1,
    .pCommandBuffers      = &command_buffer,
    .signalSemaphoreCount = signal_count,
    .pSignalSemaphores    = signal_count ? &signal_semaphore : NULL,
  };

  vk(QueueSubmit(queue, 1, &submitInfo, fence));

  if (wait_fence && fence != VK_NULL_HANDLE)
    {
      vk(WaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX));
      vk(ResetFences(device, 1, &fence));
    }
}

//
//
//

class SkiaTestAppImpl {
 public:
  SkiaTestAppImpl(const char * app_name, bool debug, uint32_t window_width, uint32_t window_height)
  {
    // Initialize Vulkan application state.
    vk_app_state_config_t app_config = {
      .app_name              = app_name ? app_name : "skia_test_app",
      .enable_validation     = debug,
      .enable_debug_report   = debug,
      .enable_amd_statistics = debug,

      .device_config = {
        .required_queues = VK_QUEUE_GRAPHICS_BIT,
      },

      .require_swapchain     = true,
    };

    ASSERT(vk_app_state_init(&app_state_, &app_config));

    if (debug)
      vk_app_state_print(&app_state_);

    // Initialize Vulkan swapchain
    vk_swapchain_config_t swapchain_config = {
      .instance        = app_state_.instance,
      .device          = app_state_.d,
      .physical_device = app_state_.pd,
      .allocator       = app_state_.ac,

      .present_queue_family  = app_state_.qfi,
      .present_queue_index   = 0,
      .graphics_queue_family = app_state_.qfi,
      .graphics_queue_index  = 0,

      .surface_khr = vk_app_state_create_surface(&app_state_, window_width, window_height),
      .max_frames  = 2,
    };
    swapchain_ = vk_swapchain_create(&swapchain_config);
    ASSERT_MSG(swapchain_, "Could not create swapchain!");

    vk_swapchain_enable_image_command_buffers(swapchain_, app_state_.qfi, 0);

    if (debug)
      vk_swapchain_print(swapchain_);

    // Initialize Skia Vulkan backend.
    {
      auto &         backend = backendContext_;
      GrVkExtensions extensions;
      backend.fInstance           = app_state_.instance;
      backend.fPhysicalDevice     = app_state_.pd;
      backend.fDevice             = app_state_.d;
      backend.fGraphicsQueueIndex = app_state_.qfi;
      vkGetDeviceQueue(app_state_.d, app_state_.qfi, 0, &backend.fQueue);
      // NOTE: Skia code mentions that only this extension is relevant / tested.
      backend.fExtensions     = kKHR_swapchain_GrVkExtensionFlag;
      backend.fVkExtensions   = &extensions;
      backend.fDeviceFeatures = &deviceFeatures_;
      backend.fGetProc        = [](const char * name, VkInstance instance, VkDevice device) {
        if (device != VK_NULL_HANDLE)
          return vkGetDeviceProcAddr(device, name);
        else
          return vkGetInstanceProcAddr(instance, name);
      };

      // NOTE: Skia does not compile its Vulkan backend by default for Linux builds
      // but this can be forced by adding a line with 'skia_use_vulkan = true' in
      // your args.gn. This is required to run this demo properly.
      context_ = GrContext::MakeVulkan(backend);
      ASSERT_MSG(context_.get(),
                 "Could not initialize Skia Vulkan context\n"
#ifndef __Fuchsia__
                 "Did you use 'skia_use_vulkan = true' in your args.gn?\n"
#endif
      );

      uint32_t           image_count    = vk_swapchain_get_image_count(swapchain_);
      VkSurfaceFormatKHR surface_format = vk_swapchain_get_format(swapchain_);

      SkColorType colorType;
      switch (surface_format.format)
        {
          case VK_FORMAT_R8G8B8A8_SRGB:  // fall through
          case VK_FORMAT_R8G8B8A8_UNORM:
            colorType = kRGBA_8888_SkColorType;
            break;
          case VK_FORMAT_B8G8R8A8_UNORM:
          case VK_FORMAT_B8G8R8A8_SRGB:
            colorType = kBGRA_8888_SkColorType;
            break;
          default:
            ASSERT_MSG(false, "Unsupported surface_format: %u\n", surface_format.format);
        }

      image_surfaces_.resize(image_count);
      for (uint32_t nn = 0; nn < image_count; ++nn)
        {
          GrVkImageInfo info;
          info.fImage              = vk_swapchain_get_image(swapchain_, nn);
          info.fAlloc              = GrVkAlloc();
          info.fImageLayout        = VK_IMAGE_LAYOUT_UNDEFINED;
          info.fImageTiling        = VK_IMAGE_TILING_OPTIMAL;
          info.fFormat             = surface_format.format;
          info.fLevelCount         = 1;
          info.fCurrentQueueFamily = VK_QUEUE_FAMILY_IGNORED;

          VkExtent2D            surface_extent = vk_swapchain_get_extent(swapchain_);
          GrBackendRenderTarget backendRT(surface_extent.width, surface_extent.height, 1, info);
          image_surfaces_[nn] = SkSurface::MakeFromBackendRenderTarget(context_.get(),
                                                                       backendRT,
                                                                       kTopLeft_GrSurfaceOrigin,
                                                                       colorType,
                                                                       nullptr /* colorSpace */,
                                                                       nullptr /* surfaceProps */);
          ASSERT(image_surfaces_[nn].get());
        }
    }

    // Create sync fence.
    {
      const VkFenceCreateInfo fenceInfo = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
      };
      vk(CreateFence(app_state_.d, &fenceInfo, app_state_.ac, &sync_fence_));
    }
  }

  ~SkiaTestAppImpl()
  {
    // Destroy sync_fence_
    vkDestroyFence(app_state_.d, sync_fence_, app_state_.ac);

    image_surfaces_.clear();
    context_.reset();

    vk_swapchain_destroy(swapchain_);
    vk_app_state_destroy(&app_state_);
  }

  void
  run(std::function<void(SkCanvas *, uint32_t)> drawFrameFunc)
  {
    prepareSwapchainImages();

    VkQueue graphics_queue = vk_swapchain_get_graphics_queue(swapchain_);

    // Main loop
    uint32_t frame_counter = 0;
    while (vk_app_state_poll_events(&app_state_))
      {
        uint32_t image_index;
        auto *   swapchain = swapchain_;
        auto     device    = app_state_.d;
        //auto allocator = app_state_.ac;

        if (!vk_swapchain_prepare_next_image(swapchain, &image_index))
          {
            // Window was resized! For now just exit!!
            // TODO(digit): Handle resize!!
            break;
          }

        // Transition image to COLOR_ATTACHMENT_OPTIMAL before drawing.
        VkImage         swapchain_image = vk_swapchain_get_image(swapchain, image_index);
        VkCommandBuffer command_buffer =
          vk_swapchain_get_image_command_buffer(swapchain, image_index);
        {
          command_buffer_begin(command_buffer);
          image_layout_transition(command_buffer,
                                  swapchain_image,
                                  VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
          vk(EndCommandBuffer(command_buffer));

          command_buffer_submit(command_buffer,
                                graphics_queue,
                                device,
                                vk_swapchain_get_image_acquired_semaphore(swapchain),
                                VK_NULL_HANDLE,
                                sync_fence_,
                                true);
        }

        // Draw frame with Skia.
        drawFrameFunc(image_surfaces_[image_index]->getCanvas(), frame_counter);

        // Transition image to presentation format after drawing.
        // And ensure that the swapchain fence is signaled when this completes.
        {
          command_buffer_begin(command_buffer);
          image_layout_transition(command_buffer,
                                  swapchain_image,
                                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                  VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
          vk(EndCommandBuffer(command_buffer));
          command_buffer_submit(command_buffer,
                                graphics_queue,
                                device,
                                VK_NULL_HANDLE,
                                vk_swapchain_get_image_rendered_semaphore(swapchain),
                                vk_swapchain_get_image_rendered_fence(swapchain),
                                false);
        }

        // Present image.
        vk_swapchain_present_image(swapchain);

        // Print a small tick every two seconds (assuming a 60hz swapchain) to
        // check that everything is working, even if the image is static at this
        // point.
        if (++frame_counter % (60 * 2) == 0)
          {
            printf("!");
            fflush(stdout);
          }
      }
    vkDeviceWaitIdle(app_state_.d);
  }

 private:
  // Transition all swapchain images to VK_IMAGE_LAYOUT_PRESENT_SRC_KHR.
  void
  prepareSwapchainImages() const
  {
    vk_swapchain_t * swapchain   = swapchain_;
    VkDevice         device      = app_state_.d;
    uint32_t         image_count = vk_swapchain_get_image_count(swapchain);
    for (uint32_t nn = 0; nn < image_count; ++nn)
      {
        VkCommandBuffer command_buffer  = vk_swapchain_get_image_command_buffer(swapchain, nn);
        VkImage         swapchain_image = vk_swapchain_get_image(swapchain, nn);

        command_buffer_begin(command_buffer);
        image_layout_transition(command_buffer,
                                swapchain_image,
                                VK_IMAGE_LAYOUT_UNDEFINED,
                                VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
        vk(EndCommandBuffer(command_buffer));

        command_buffer_submit(command_buffer,
                              vk_swapchain_get_graphics_queue(swapchain),
                              device,
                              VK_NULL_HANDLE,  // wait_semaphore
                              VK_NULL_HANDLE,  // signal_semaphore
                              sync_fence_,
                              true);
      }
  }

  std::vector<sk_sp<SkSurface>> image_surfaces_;  // one per swapchain image
  sk_sp<GrContext>              context_        = nullptr;
  GrVkBackendContext            backendContext_ = {};
  VkPhysicalDeviceFeatures      deviceFeatures_ = {};

  VkFence          sync_fence_ = VK_NULL_HANDLE;
  vk_swapchain_t * swapchain_  = NULL;
  vk_app_state_t   app_state_  = {};
};

SkiaTestApp::SkiaTestApp(const char * app_name,
                         bool         is_debug,
                         uint32_t     window_width,
                         uint32_t     window_height)
    : impl_(new SkiaTestAppImpl(app_name, is_debug, window_width, window_height))
{
}

SkiaTestApp::~SkiaTestApp()
{
  delete impl_;
}

void
SkiaTestApp::run()
{
  impl_->run(
    [this](SkCanvas * canvas, uint32_t frame_counter) { this->drawFrame(canvas, frame_counter); });
}
