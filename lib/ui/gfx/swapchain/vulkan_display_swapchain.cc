// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/swapchain/vulkan_display_swapchain.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <trace/event.h>

#include "garnet/lib/ui/gfx/displays/display.h"
#include "garnet/lib/ui/gfx/engine/frame_timings.h"

#include "lib/escher/escher.h"
#include "lib/escher/util/fuchsia_utils.h"
#include "lib/escher/vk/gpu_mem.h"

namespace scenic {
namespace gfx {

namespace {

#define VK_CHECK_RESULT(XXX) FXL_CHECK(XXX.result == vk::Result::eSuccess)

const uint32_t kDesiredSwapchainImageCount = 2;

}  // namespace

VulkanDisplaySwapchain::VulkanDisplaySwapchain(Display* display,
                                               EventTimestamper* timestamper,
                                               escher::Escher* escher)
    : display_(display),
      event_timestamper_(timestamper),
      device_(escher->vk_device()),
      queue_(escher->device()->vk_main_queue()) {
  display_->Claim();

  InitializeVulkanSwapchain(display_, escher->device(),
                            escher->resource_recycler());

  image_available_semaphores_.reserve(swapchain_.images.size());
  render_finished_semaphores_.reserve(swapchain_.images.size());
  for (size_t i = 0; i < swapchain_.images.size(); ++i) {
// TODO: Use timestamper to listen for event notifications
#if 1
    image_available_semaphores_.push_back(escher::Semaphore::New(device_));
    render_finished_semaphores_.push_back(escher::Semaphore::New(device_));
#else
    auto pair = NewSemaphoreEventPair(escher);
    image_available_semaphores_.push_back(std::move(pair.first));
    watches_.push_back(
        timestamper, std::move(pair.second), kFenceSignalled,
        [this, i](zx_time_t timestamp) { OnFramePresented(i, timestamp); });
#endif
  }
}

VulkanDisplaySwapchain::~VulkanDisplaySwapchain() {
  swapchain_.images.clear();

  FXL_CHECK(swapchain_.swapchain);
  device_.destroySwapchainKHR(swapchain_.swapchain);
  swapchain_.swapchain = nullptr;

  display_->Unclaim();
}

void VulkanDisplaySwapchain::InitializeVulkanSwapchain(
    Display* display, escher::VulkanDeviceQueues* device_queues,
    escher::ResourceRecycler* recycler) {
  vk::PhysicalDevice physical_device = device_queues->vk_physical_device();
  vk::SurfaceKHR surface = device_queues->vk_surface();
  FXL_CHECK(!swapchain_.swapchain);
  FXL_CHECK(swapchain_.images.empty());
  FXL_CHECK(recycler);

  {
    auto result = physical_device.getSurfaceSupportKHR(
        device_queues->vk_main_queue_family(), surface);
    VK_CHECK_RESULT(result);
    FXL_CHECK(result.value);
  }

  vk::SurfaceCapabilitiesKHR surface_caps;
  {
    auto result = physical_device.getSurfaceCapabilitiesKHR(surface);
    VK_CHECK_RESULT(result);
    surface_caps = std::move(result.value);
  }

  std::vector<vk::PresentModeKHR> present_modes;
  {
    auto result = physical_device.getSurfacePresentModesKHR(surface);
    VK_CHECK_RESULT(result);
    present_modes = std::move(result.value);
  }

  // TODO: handle undefined width/height.
  vk::Extent2D swapchain_extent = surface_caps.currentExtent;
  constexpr uint32_t VK_UNDEFINED_WIDTH_OR_HEIGHT = 0xFFFFFFFF;
  if (swapchain_extent.width == VK_UNDEFINED_WIDTH_OR_HEIGHT) {
    swapchain_extent.width = display->width_in_px();
  }
  if (swapchain_extent.height == VK_UNDEFINED_WIDTH_OR_HEIGHT) {
    swapchain_extent.height = display->height_in_px();
  }
  FXL_CHECK(swapchain_extent.width == display->width_in_px());
  FXL_CHECK(swapchain_extent.height == display->height_in_px());

  // FIFO mode is always available, but we will try to find a more efficient
  // mode.
  vk::PresentModeKHR swapchain_present_mode = vk::PresentModeKHR::eFifo;
// TODO: Find out why these modes are causing lower performance on Skylake
#if 0
  for (auto& mode : present_modes) {
    if (mode == vk::PresentModeKHR::eMailbox) {
      // Best choice: lowest-latency non-tearing mode.
      swapchain_present_mode = vk::PresentModeKHR::eMailbox;
      break;
    }
    if (mode == vk::PresentModeKHR::eImmediate) {
      // Satisfactory choice: fastest, but tears.
      swapchain_present_mode = vk::PresentModeKHR::eImmediate;
    }
  }
#endif

  // Determine number of images in the swapchain.
  swapchain_image_count_ = kDesiredSwapchainImageCount;
  if (surface_caps.minImageCount > swapchain_image_count_) {
    swapchain_image_count_ = surface_caps.minImageCount;
  } else if (surface_caps.maxImageCount < swapchain_image_count_ &&
             surface_caps.maxImageCount != 0) {  // 0 means "no limit"
    swapchain_image_count_ = surface_caps.maxImageCount;
  }

  // TODO: choosing an appropriate pre-transform will probably be important on
  // mobile devices.
  auto pre_transform = vk::SurfaceTransformFlagBitsKHR::eIdentity;

  // Pick a format and color-space for the swap-chain.
  vk::Format format = vk::Format::eUndefined;
  vk::ColorSpaceKHR color_space = vk::ColorSpaceKHR::eSrgbNonlinear;
  {
    auto result = physical_device.getSurfaceFormatsKHR(surface);
    VK_CHECK_RESULT(result);
    for (auto& sf : result.value) {
      if (sf.colorSpace != color_space)
        continue;

      // TODO: remove this once Magma supports SRGB swapchains.
      if (sf.format == vk::Format::eB8G8R8A8Unorm) {
        format = sf.format;
        break;
      }

      if (sf.format == vk::Format::eB8G8R8A8Srgb) {
        // eB8G8R8A8Srgb is our favorite!
        format = sf.format;
        break;
      } else if (format == vk::Format::eUndefined) {
        // Anything is better than eUndefined.
        format = sf.format;
      }
    }
  }
  FXL_CHECK(format != vk::Format::eUndefined);

  // TODO: old_swapchain will come into play (I think) when we support
  // resizing the window.
  vk::SwapchainKHR old_swapchain = nullptr;

  // Create the swapchain.
  vk::SwapchainKHR swapchain;
  {
    vk::SwapchainCreateInfoKHR info;
    info.surface = surface;
    info.minImageCount = swapchain_image_count_;
    info.imageFormat = format;
    info.imageColorSpace = color_space;
    info.imageExtent = swapchain_extent;
    info.imageArrayLayers = 1;  // TODO: what is this?
    // Using eTransferDst allows us to blit debug info onto the surface.
    // Using eSampled allows us to save memory by using the color attachment
    // for intermediate computation.
    info.imageUsage = vk::ImageUsageFlagBits::eColorAttachment |
                      vk::ImageUsageFlagBits::eTransferDst |
                      vk::ImageUsageFlagBits::eSampled;
    info.queueFamilyIndexCount = 1;
    uint32_t queue_family_index = device_queues->vk_main_queue_family();
    info.pQueueFamilyIndices = &queue_family_index;
    info.preTransform = pre_transform;
    info.presentMode = swapchain_present_mode;
    info.oldSwapchain = old_swapchain;
    info.clipped = true;

    auto result = device_.createSwapchainKHR(info);
    VK_CHECK_RESULT(result);
    swapchain = result.value;
  }

  if (old_swapchain) {
    // Note: destroying the swapchain also cleans up all its associated
    // presentable images once the platform is done with them.
    device_.destroySwapchainKHR(old_swapchain);
  }

  // Obtain swapchain images and buffers.
  {
    auto result = device_.getSwapchainImagesKHR(swapchain);
    VK_CHECK_RESULT(result);

    std::vector<vk::Image> images(std::move(result.value));
    std::vector<escher::ImagePtr> escher_images;
    escher_images.reserve(images.size());
    for (auto& im : images) {
      escher::ImageInfo image_info;
      image_info.format = format;
      image_info.width = swapchain_extent.width;
      image_info.height = swapchain_extent.height;
      image_info.usage = vk::ImageUsageFlagBits::eColorAttachment;
      auto escher_image = escher::Image::New(recycler, image_info, im, nullptr);
      FXL_CHECK(escher_image);
      escher_images.push_back(escher_image);
    }
    swapchain_ = escher::VulkanSwapchain(
        swapchain, escher_images, swapchain_extent.width,
        swapchain_extent.height, format, color_space);
  }
}

bool VulkanDisplaySwapchain::DrawAndPresentFrame(
    const FrameTimingsPtr& frame_timings, DrawCallback draw_callback) {
  // TODO(MZ-260): replace Vulkan swapchain with Magma C ABI calls, and use
  // EventTimestamper::Wait to notify |frame| when the frame is finished
  // rendering, and when it is presented.
  auto timing_index = frame_timings->AddSwapchain(this);
  if (event_timestamper_ && !event_timestamper_) {
    // Avoid unused-variable error.
    FXL_CHECK(false) << "I don't believe you.";
  }

  auto& image_available_semaphore =
      image_available_semaphores_[next_semaphore_index_];
  auto& render_finished_semaphore =
      render_finished_semaphores_[next_semaphore_index_];

  uint32_t swapchain_index;
  {
    TRACE_DURATION("gfx", "VulkanDisplaySwapchain::DrawAndPresent() acquire");

    auto result = device_.acquireNextImageKHR(
        swapchain_.swapchain, UINT64_MAX,
        image_available_semaphore->vk_semaphore(), nullptr);

    if (result.result == vk::Result::eSuboptimalKHR) {
      FXL_DLOG(WARNING) << "suboptimal swapchain configuration";
    } else if (result.result != vk::Result::eSuccess) {
      FXL_LOG(WARNING) << "failed to acquire next swapchain image"
                       << " : " << to_string(result.result);
      return false;
    }

    swapchain_index = result.value;
    next_semaphore_index_ =
        (next_semaphore_index_ + 1) % swapchain_.images.size();
  }

  // Render the scene.  The Renderer will wait for acquireNextImageKHR() to
  // signal the semaphore.
  draw_callback(swapchain_.images[swapchain_index], image_available_semaphore,
                render_finished_semaphore);

  // When the image is completely rendered, present it.
  TRACE_DURATION("gfx", "VulkanDisplaySwapchain::DrawAndPresent() present");
  vk::PresentInfoKHR info;
  info.waitSemaphoreCount = 1;
  auto sema = render_finished_semaphore->vk_semaphore();
  info.pWaitSemaphores = &sema;
  info.swapchainCount = 1;
  info.pSwapchains = &swapchain_.swapchain;
  info.pImageIndices = &swapchain_index;

  // TODO(MZ-244): handle this more robustly.
  if (queue_.presentKHR(info) != vk::Result::eSuccess) {
    FXL_DCHECK(false)
        << "VulkanDisplaySwapchain::DrawAndPresentFrame(): failed to "
           "present rendered image.";
  }
  // TODO: Wait for sema before triggering callbacks. This class is only used
  // for debugging, so the precise timestamps don't matter as much.
  async::PostTask(async_get_default_dispatcher(), [frame_timings, timing_index] {
    frame_timings->OnFrameRendered(timing_index,
                                   zx_clock_get(ZX_CLOCK_MONOTONIC));
    frame_timings->OnFramePresented(timing_index,
                                    zx_clock_get(ZX_CLOCK_MONOTONIC));
  });

  return true;
}

}  // namespace gfx
}  // namespace scenic
