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
#include "include/gpu/GrBackendSemaphore.h"
#include "include/gpu/GrContext.h"
#include "include/gpu/vk/GrVkBackendContext.h"
#include "include/gpu/vk/GrVkExtensions.h"

//
//  Helper functions
//

//
//
//

class SkiaTestAppImpl {
 public:
  SkiaTestAppImpl(const SkiaTestApp::Config & config) : config_(config)
  {
    // Initialize Vulkan application state.
    vk_app_state_config_t app_config = {
      .app_name              = config.app_name ? config.app_name : "skia_test_app",
      .enable_validation     = config.enable_debug,
      .enable_debug_report   = config.enable_debug,
      .enable_amd_statistics = config.enable_debug,

      .device_config = {
        .required_queues = VK_QUEUE_GRAPHICS_BIT,
      },

      .require_swapchain         = true,
      .disable_swapchain_present = config.disable_vsync,
    };

    ASSERT(vk_app_state_init(&app_state_, &app_config));

    if (config.enable_debug)
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

      .surface_khr =
        vk_app_state_create_surface(&app_state_, config.window_width, config.window_height),
      .max_frames              = 3,
      .disable_vsync           = config.disable_vsync,
      .use_presentation_layout = true,
    };
    swapchain_ = vk_swapchain_create(&swapchain_config);
    ASSERT_MSG(swapchain_, "Could not create swapchain!");

    if (config.enable_debug)
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

      images_.resize(image_count);
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

          VkExtent2D surface_extent = vk_swapchain_get_extent(swapchain_);

          images_[nn].render_target.reset(
            new GrBackendRenderTarget(surface_extent.width, surface_extent.height, 1, info));
          images_[nn].surface = SkSurface::MakeFromBackendRenderTarget(context_.get(),
                                                                       *images_[nn].render_target,
                                                                       kTopLeft_GrSurfaceOrigin,
                                                                       colorType,
                                                                       nullptr /* colorSpace */,
                                                                       nullptr /* surfaceProps */);
          ASSERT(images_[nn].surface.get());
        }
    }
  }

  ~SkiaTestAppImpl()
  {
    images_.clear();
    context_.reset();

    vk_swapchain_destroy(swapchain_);
    vk_app_state_destroy(&app_state_);
  }

  void
  run(std::function<void(SkCanvas *, uint32_t)> drawFrameFunc)
  {
    // Main loop
    uint32_t frame_counter = 0;
    while (vk_app_state_poll_events(&app_state_))
      {
        auto * swapchain = swapchain_;

        uint32_t image_index;
        if (!vk_swapchain_acquire_next_image(swapchain, &image_index))
          break;

        auto const & surface = images_[image_index].surface;

        drawFrameFunc(surface->getCanvas(), frame_counter);

        GrBackendSemaphore skiaWaitSemaphore;
        skiaWaitSemaphore.initVulkan(vk_swapchain_take_image_acquired_semaphore(swapchain));
        surface->wait(1, &skiaWaitSemaphore);

        GrBackendSemaphore skiaSignalSemaphore;
        skiaSignalSemaphore.initVulkan(vk_swapchain_get_image_rendered_semaphore(swapchain));

        const GrFlushInfo flushInfo = {
          .fFlags            = kNone_GrFlushFlags,
          .fNumSemaphores    = 1,
          .fSignalSemaphores = &skiaSignalSemaphore,
        };

        surface->flush(SkSurface::BackendSurfaceAccess::kPresent, flushInfo);

        vk_swapchain_present_image(swapchain);

        // Print a small tick every two seconds (assuming a 60hz swapchain) to
        // check that everything is working, even if the image is static at this
        // point.
        if (config_.enable_debug && frame_counter > 0 && frame_counter % (60 * 2) == 0)
          {
            printf("!");
            fflush(stdout);
          }

        frame_counter++;
      }
    vkDeviceWaitIdle(app_state_.d);
  }

 private:
  struct SwapchainImage
  {
    std::unique_ptr<GrBackendRenderTarget> render_target;
    sk_sp<SkSurface>                       surface;
  };

  SkiaTestApp::Config         config_;
  vk_app_state_t              app_state_ = {};
  vk_swapchain_t *            swapchain_ = NULL;
  std::vector<SwapchainImage> images_;  // one per swapchain image
  sk_sp<GrContext>            context_        = nullptr;
  GrVkBackendContext          backendContext_ = {};
  VkPhysicalDeviceFeatures    deviceFeatures_ = {};
};

SkiaTestApp::SkiaTestApp(const SkiaTestApp::Config & config) : impl_(new SkiaTestAppImpl(config))
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
