// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/scene_manager/swapchain/display_swapchain.h"

#include <trace/event.h>

#include "garnet/bin/ui/scene_manager/displays/display.h"
#include "garnet/bin/ui/scene_manager/engine/frame_timings.h"
#include "garnet/bin/ui/scene_manager/sync/fence.h"
#include "garnet/bin/ui/scene_manager/util/escher_utils.h"

#include "lib/escher/escher.h"
#include "lib/escher/util/image_utils.h"
#include "lib/escher/vk/gpu_mem.h"

namespace scene_manager {

namespace {

#define VK_CHECK_RESULT(XXX) FXL_CHECK(XXX.result == vk::Result::eSuccess)

const uint32_t kDesiredSwapchainImageCount = 2;

// Helper functions

// Enumerate the formats supported for the specified surface/device, and pick a
// suitable one.
vk::Format GetDisplayImageFormat(escher::VulkanDeviceQueues* device_queues);

// Determines if the VK_GOOGLE_IMAGE_TILING_SCANOUT_EXTENSION is supported.
vk::ImageTiling GetFramebufferImageTiling();

// Exports a Semaphore into an event.
// TODO(ES-40): Factor this into an Escher Fuchsia support library.
zx::event GetEventForSemaphore(
    const escher::VulkanDeviceQueues::ProcAddrs& proc_addresses,
    const vk::Device& device,
    const escher::SemaphorePtr& semaphore);

}  // namespace

DisplaySwapchain::DisplaySwapchain(Display* display,
                                   EventTimestamper* timestamper,
                                   escher::Escher* escher)
    : display_(display),
      event_timestamper_(timestamper),
      device_(escher->vk_device()),
      queue_(escher->device()->vk_main_queue()),
      vulkan_proc_addresses_(escher->device()->proc_addrs()) {
  display_->Claim();
  magma_connection_.Open();

  format_ = GetDisplayImageFormat(escher->device());

  image_available_semaphores_.reserve(kDesiredSwapchainImageCount);
  for (size_t i = 0; i < kDesiredSwapchainImageCount; ++i) {
// TODO: Use timestamper to listen for event notifications
#if 1

    image_available_semaphores_.push_back(
        Export(escher::Semaphore::New(device_)));

    // The images are all available initially.
    image_available_semaphores_[i].fence->event().signal(0u, kFenceSignalled);
#else
    auto pair = NewSemaphoreEventPair(escher);
    image_available_semaphores_.push_back(std::move(pair.first));
    watches_.push_back(
        timestamper, std::move(pair.second), kFenceSignalled,
        [this, i](zx_time_t timestamp) { OnFramePresented(i, timestamp); });
#endif
  }

  if (!InitializeFramebuffers(escher->resource_recycler())) {
    FXL_DLOG(ERROR) << "Initializing buffers for display swapchain failed.";
  }
}

bool DisplaySwapchain::InitializeFramebuffers(
    escher::ResourceRecycler* resource_recycler) {
  vk::ImageTiling image_tiling = GetFramebufferImageTiling();

  for (uint32_t i = 0; i < kDesiredSwapchainImageCount; i++) {
    // Allocate a framebuffer.
    uint32_t width = display_->metrics().width_in_px();
    uint32_t height = display_->metrics().height_in_px();

    // Start by creating a VkImage.
    // TODO(ES-42): Create this using Escher APIs.
    vk::ImageUsageFlags image_usage = vk::ImageUsageFlagBits::eColorAttachment;
    vk::ImageCreateInfo create_info;
    create_info.imageType = vk::ImageType::e2D, create_info.format = format_;
    create_info.extent = vk::Extent3D{width, height, 1};
    create_info.mipLevels = 1;
    create_info.arrayLayers = 1;
    create_info.samples = vk::SampleCountFlagBits::e1;
    create_info.tiling = image_tiling;
    create_info.usage = image_usage;
    create_info.sharingMode = vk::SharingMode::eExclusive;
    create_info.initialLayout = vk::ImageLayout::eUndefined;

    auto image_result = device_.createImage(create_info);
    if (image_result.result != vk::Result::eSuccess) {
      FXL_DLOG(ERROR) << "VkCreateImage failed: "
                      << vk::to_string(image_result.result);
      return false;
    }

    // Allocate memory to get a VkDeviceMemory.
    auto memory_requirements =
        device_.getImageMemoryRequirements(image_result.value);

    uint32_t memory_type_index = 0;
    vk::MemoryAllocateInfo alloc_info;
    alloc_info.allocationSize = memory_requirements.size;
    alloc_info.memoryTypeIndex = memory_type_index;

    auto mem_result = device_.allocateMemory(alloc_info);

    if (mem_result.result != vk::Result::eSuccess) {
      FXL_DLOG(ERROR) << "vkAllocMemory failed: "
                      << vk::to_string(mem_result.result);
      return false;
    }

    Framebuffer buffer;
    buffer.device_memory = escher::GpuMem::New(
        device_, mem_result.value, memory_requirements.size, memory_type_index);
    FXL_DCHECK(buffer.device_memory);

    // Wrap the image and device memory in a escher::Image.
    escher::ImageInfo image_info;
    image_info.format = format_;
    image_info.width = display_->metrics().width_in_px();
    image_info.height = display_->metrics().height_in_px();
    image_info.usage = image_usage;

    // escher::Image::New() binds the memory to the image.
    buffer.escher_image =
        escher::Image::New(resource_recycler, image_info, image_result.value,
                           buffer.device_memory);

    if (!buffer.escher_image) {
      FXL_DLOG(ERROR) << "Creating escher::EscherImage failed.";
      device_.destroyImage(image_result.value);
      return false;
    }

    // TODO(ES-39): Add stride to escher::ImageInfo so we can use
    // getImageSubresourceLayout to look up rowPitch and use it appropriately.
    /*vk::ImageSubresource subres;
    subres.aspectMask = vk::ImageAspectFlagBits::eColor;
    subres.mipLevel = 0;
    subres.arrayLayer = 0;
    auto layout = device_.getImageSubresourceLayout(image_result.value, subres);
    FXL_DCHECK(layout.rowPitch ==
               display_->width() *
    escher::image_utils::BytesPerPixel(format_));
    */

    // Export the vkDeviceMemory to a VMO.
    auto export_result =
        device_.exportMemoryMAGMA(buffer.device_memory->base());
    if (export_result.result != vk::Result::eSuccess) {
      FXL_DLOG(ERROR) << "vkExportDeviceMemoryMAGMA failed: "
                      << vk::to_string(export_result.result);
      return false;
    }

    buffer.vmo = zx::vmo(export_result.value);
    buffer.magma_buffer =
        MagmaBuffer::NewFromVmo(&magma_connection_, buffer.vmo);
    swapchain_buffers_.push_back(std::move(buffer));
  }
  return true;
}

DisplaySwapchain::~DisplaySwapchain() {
  display_->Unclaim();
}

DisplaySwapchain::Semaphore DisplaySwapchain::Export(
    escher::SemaphorePtr escher_semaphore) {
  Semaphore semaphore;
  semaphore.escher_semaphore = escher_semaphore;
  zx::event fence =
      GetEventForSemaphore(vulkan_proc_addresses_, device_, escher_semaphore);
  FXL_CHECK(fence);
  semaphore.magma_semaphore =
      MagmaSemaphore::NewFromEvent(&magma_connection_, fence);
  semaphore.fence = std::make_unique<FenceListener>(std::move(fence));
  return semaphore;
}

bool DisplaySwapchain::DrawAndPresentFrame(const FrameTimingsPtr& frame_timings,
                                           DrawCallback draw_callback) {
  // TODO(MZ-260): Use EventTimestamper::Wait to notify |frame_timings| when the
  // frame is finished finished rendering, and when it is presented.
  //
  // auto timing_index = frame_timings->AddSwapchain(this);
  if (event_timestamper_ && !event_timestamper_) {
    // Avoid unused-variable error.
    FXL_CHECK(false) << "I don't believe you.";
  }

  // Obtain a semaphore to wait for the next available image, and
  // replace it with another semaphore that will be signaled when
  // the about-to-be-rendered frame is no longer used.
  auto image_available_semaphore =
      std::move(image_available_semaphores_[next_semaphore_index_]);
  image_available_semaphores_[next_semaphore_index_] =
      Export(escher::Semaphore::New(device_));
  auto& image_available_next_frame_semaphore =
      image_available_semaphores_[next_semaphore_index_];
  auto render_finished = Export(escher::Semaphore::New(device_));

  auto& buffer = swapchain_buffers_[next_semaphore_index_];

  next_semaphore_index_ =
      (next_semaphore_index_ + 1) % kDesiredSwapchainImageCount;

  {
    TRACE_DURATION("gfx", "VulkanDisplaySwapchain::DrawAndPresent() acquire");

    // TODO(MZ-260): once FrameScheduler back-pressure is implemented,
    // it will no longer be necessary to wait for the image to become
    // available (this is currently done to avoid a backlog of frames that
    // we cannot keep up with).
    image_available_semaphore.fence->WaitReady();
  }

  // Render the scene.
  {
    TRACE_DURATION("gfx", "DisplaySwapchain::DrawAndPresent() draw");
    draw_callback(buffer.escher_image,
                  image_available_semaphore.escher_semaphore,
                  render_finished.escher_semaphore);
  }

  // When the image is completely rendered, present it.
  TRACE_DURATION("gfx", "DisplaySwapchain::DrawAndPresent() present");

  // Present semaphore
  Semaphore present_semaphore = Export(escher::Semaphore::New(device_));

  bool status = magma_connection_.DisplayPageFlip(
      buffer.magma_buffer.get(), 1, &render_finished.magma_semaphore.get(), 1,
      &image_available_next_frame_semaphore.magma_semaphore.get(),
      present_semaphore.magma_semaphore.get());

  // TODO(MZ-244): handle this more robustly.
  if (!status) {
    FXL_DCHECK(false) << "DisplaySwapchain::DrawAndPresentFrame(): failed to"
                         "present rendered image with magma_display_page_flip.";
    return false;
  }

  return true;
}

namespace {

vk::ImageTiling GetFramebufferImageTiling() {
  uint32_t instance_extension_count;
  vk::Result enumerate_result =
      vk::enumerateInstanceLayerProperties(&instance_extension_count, nullptr);
  if (enumerate_result != vk::Result::eSuccess) {
    FXL_DLOG(ERROR) << "vkEnumerateInstanceLayerProperties failed: "
                    << vk::to_string(enumerate_result);
    return vk::ImageTiling::eOptimal;
  }

  if (instance_extension_count > 0) {
    auto instance_extensions = vk::enumerateInstanceExtensionProperties();
    if (instance_extensions.result != vk::Result::eSuccess) {
      FXL_DLOG(ERROR) << "vkEnumerateInstanceExtensionProperties failed: "
                      << vk::to_string(instance_extensions.result);
      return vk::ImageTiling::eOptimal;
    }

    const std::string kGoogleImageTilingScanoutExtensionName(
        VK_GOOGLE_IMAGE_TILING_SCANOUT_EXTENSION_NAME);
    for (auto& extension : instance_extensions.value) {
      if (extension.extensionName == kGoogleImageTilingScanoutExtensionName) {
        return vk::ImageTiling::eScanoutGOOGLE;
      }
    }
  }
  return vk::ImageTiling::eOptimal;
}

vk::Format GetDisplayImageFormat(escher::VulkanDeviceQueues* device_queues) {
  vk::PhysicalDevice physical_device = device_queues->vk_physical_device();
  vk::SurfaceKHR surface = device_queues->vk_surface();
  FXL_DCHECK(surface);

  // Pick a format and color-space for the swapchain.
  vk::Format format = vk::Format::eUndefined;
  vk::ColorSpaceKHR color_space = vk::ColorSpaceKHR::eSrgbNonlinear;
  {
    auto result = physical_device.getSurfaceFormatsKHR(surface);
    VK_CHECK_RESULT(result);
    for (auto& sf : result.value) {
      if (sf.colorSpace != color_space)
        continue;

      // TODO(MZ-382): remove this once Magma supports SRGB swapchains (MA-135).
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
  return format;
}

zx::event GetEventForSemaphore(
    const escher::VulkanDeviceQueues::ProcAddrs& proc_addresses,
    const vk::Device& device,
    const escher::SemaphorePtr& semaphore) {
  zx_handle_t semaphore_handle;

  VkSemaphoreGetFuchsiaHandleInfoKHR info = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FUCHSIA_HANDLE_INFO_KHR,
      .pNext = nullptr,
      .semaphore = semaphore->vk_semaphore(),
      .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_FUCHSIA_FENCE_BIT_KHR,
  };
  VkResult result = proc_addresses.GetSemaphoreFuchsiaHandleKHR(
      device, &info, &semaphore_handle);

  if (result != VK_SUCCESS) {
    FXL_LOG(WARNING) << "unable to export semaphore";
    return zx::event();
  }
  return zx::event(semaphore_handle);
}

}  // namespace

}  // namespace scene_manager
