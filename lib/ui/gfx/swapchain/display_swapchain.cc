// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/swapchain/display_swapchain.h"

#include <trace/event.h>

#include "garnet/lib/ui/gfx/displays/display.h"
#include "garnet/lib/ui/gfx/displays/display_manager.h"
#include "garnet/lib/ui/gfx/engine/frame_timings.h"

#include "lib/escher/escher.h"
#include "lib/escher/flib/fence.h"
#include "lib/escher/util/fuchsia_utils.h"
#include "lib/escher/util/image_utils.h"
#include "lib/escher/vk/gpu_mem.h"

namespace scenic {
namespace gfx {

namespace {

#define VK_CHECK_RESULT(XXX) FXL_CHECK(XXX.result == vk::Result::eSuccess)

// TODO(MZ-400): Don't triple buffer.  This is done to avoid "tearing", but it
// wastes memory, and can result in the "permanent" addition of an extra Vsync
// period of latency.  An alternative would be to use an acquire fence; this
// saves memory, but can still result in the permanent extra latency.  Here's
// how:
//
// First, let's see how tearing occurs in the 2-framebuffer case.
//
// Let's say we have framebuffers A and B in a world that conveniently starts at
// some negative time, such that the first frame rendered into A has a target
// presentation time of 0ms, and the next frame is rendered into B with a target
// presentation time of 16ms.
//
// However, assume that frame being rendered into A takes a bit too long, so
// that instead of being presented at 0ms, it is instead presented at 16ms.  The
// frame to render into B has already been scheduled, and starts rendering at
// 8ms to hit the target presentation time of 16ms.  Even if it's fast, it
// cannot present at 16ms, because that frame has already been "claimed" by A,
// and so it is instead presented at 32ms.
//
// The tearing occurs when it is time to render A again.  We don't know that B
// has been deferred to present at 32ms.  So, we wake up at 24ms to render into
// A to hit the 32ms target.  Oops!
//
// The problem is that A is still being displayed from 16-32ms, until it is
// replaced by B at 32ms.  Thus, tearing.
//
// If you followed that, it should be clear both why triple-buffering fixes the
// tearing, and why it adds the frame of latency.
const uint32_t kSwapchainImageCount = 3;

// Helper functions

// Enumerate the formats supported for the specified surface/device, and pick a
// suitable one.
vk::Format GetDisplayImageFormat(escher::VulkanDeviceQueues* device_queues);

// Determines if the VK_GOOGLE_IMAGE_USAGE_SCANOUT_EXTENSION is supported.
vk::ImageUsageFlags GetFramebufferImageUsage();

}  // namespace

DisplaySwapchain::DisplaySwapchain(DisplayManager* display_manager,
                                   Display* display,
                                   EventTimestamper* timestamper,
                                   escher::Escher* escher)
    : display_manager_(display_manager),
      display_(display),
      device_(escher->vk_device()),
      queue_(escher->device()->vk_main_queue()),
      vulkan_proc_addresses_(escher->device()->proc_addrs()),
      timestamper_(timestamper) {
  FXL_DCHECK(display);
  FXL_DCHECK(timestamper);
  FXL_DCHECK(escher);

  display_->Claim();

  format_ = GetDisplayImageFormat(escher->device());

  frames_.resize(kSwapchainImageCount);

  if (!InitializeFramebuffers(escher->resource_recycler())) {
    FXL_LOG(ERROR) << "Initializing buffers for display swapchain failed.";
  }
}

bool DisplaySwapchain::InitializeFramebuffers(
    escher::ResourceRecycler* resource_recycler) {
  vk::ImageUsageFlags image_usage = GetFramebufferImageUsage();

#if !defined(__x86_64__)
  FXL_DLOG(ERROR) << "Display swapchain only supported on intel";
  return false;
#endif

  const uint32_t width_in_px = display_->width_in_px();
  const uint32_t height_in_px = display_->height_in_px();
  for (uint32_t i = 0; i < kSwapchainImageCount; i++) {
    // Allocate a framebuffer.

    // Start by creating a VkImage.
    // TODO(ES-42): Create this using Escher APIs.
    vk::ImageCreateInfo create_info;
    create_info.imageType = vk::ImageType::e2D, create_info.format = format_;
    create_info.extent = vk::Extent3D{width_in_px, height_in_px, 1};
    create_info.mipLevels = 1;
    create_info.arrayLayers = 1;
    create_info.samples = vk::SampleCountFlagBits::e1;
    create_info.tiling = vk::ImageTiling::eOptimal;
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
    image_info.width = width_in_px;
    image_info.height = height_in_px;
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
    vk::MemoryGetFuchsiaHandleInfoKHR export_memory_info(
        buffer.device_memory->base(),
        vk::ExternalMemoryHandleTypeFlagBitsKHR::eFuchsiaVmo);

    auto export_result = vulkan_proc_addresses_.getMemoryFuchsiaHandleKHR(
        device_, export_memory_info);

    if (export_result.result != vk::Result::eSuccess) {
      FXL_DLOG(ERROR) << "VkGetMemoryFuchsiaHandleKHR failed: "
                      << vk::to_string(export_result.result);
      return false;
    }

    buffer.vmo = zx::vmo(export_result.value);
    buffer.fb_id = display_manager_->ImportImage(
        buffer.vmo, width_in_px, height_in_px, ZX_PIXEL_FORMAT_ARGB_8888);
    if (buffer.fb_id == fuchsia::display::invalidId) {
      FXL_DLOG(ERROR) << "Importing image failed.";
      return false;
    }

    swapchain_buffers_.push_back(std::move(buffer));
  }
  return true;
}

DisplaySwapchain::~DisplaySwapchain() {
  display_->Unclaim();
  for (auto& buffer : swapchain_buffers_) {
    display_manager_->ReleaseImage(buffer.fb_id);
  }
}

std::unique_ptr<DisplaySwapchain::FrameRecord> DisplaySwapchain::NewFrameRecord(
    const FrameTimingsPtr& frame_timings) {
  auto render_finished_escher_semaphore =
      escher::Semaphore::NewExportableSem(device_);

  zx::event render_finished_event = GetEventForSemaphore(
      vulkan_proc_addresses_, device_, render_finished_escher_semaphore);
  uint64_t render_finished_event_id =
      display_manager_->ImportEvent(render_finished_event);

  zx::event frame_presented_event;
  zx_status_t status = zx::event::create(0u, &frame_presented_event);
  if (status != ZX_OK) {
    FXL_LOG(ERROR)
        << "DisplaySwapchain::NewFrameRecord() failed to create event.";
    return std::unique_ptr<FrameRecord>();
  }
  uint64_t frame_presented_event_id =
      display_manager_->ImportEvent(frame_presented_event);

  if (!render_finished_escher_semaphore ||
      render_finished_event_id == fuchsia::display::invalidId ||
      frame_presented_event_id == fuchsia::display::invalidId) {
    FXL_LOG(ERROR)
        << "DisplaySwapchain::NewFrameRecord() failed to create semaphores";
    return std::unique_ptr<FrameRecord>();
  }

  auto record = std::make_unique<FrameRecord>();
  record->frame_timings = frame_timings;
  record->swapchain_index = frame_timings->AddSwapchain(this);
  record->render_finished_escher_semaphore =
      std::move(render_finished_escher_semaphore);
  record->render_finished_event_id = render_finished_event_id;
  record->frame_presented_event_id = frame_presented_event_id;

  record->render_finished_watch = EventTimestamper::Watch(
      timestamper_, std::move(render_finished_event), escher::kFenceSignalled,
      [this, index = next_frame_index_](zx_time_t timestamp) {
        OnFrameRendered(index, timestamp);
      });
  record->frame_presented_watch = EventTimestamper::Watch(
      timestamper_, std::move(frame_presented_event), escher::kFenceSignalled,
      [this, index = next_frame_index_](zx_time_t timestamp) {
        OnFramePresented(index, timestamp);
      });

  return record;
}

bool DisplaySwapchain::DrawAndPresentFrame(const FrameTimingsPtr& frame_timings,
                                           DrawCallback draw_callback) {
  // Find the next framebuffer to render into, and other corresponding data.
  auto& buffer = swapchain_buffers_[next_frame_index_];

  // Create a record that can be used to notify |frame_timings| (and hence
  // ultimately the FrameScheduler) that the frame has been presented.
  //
  // There must not already exist a record.  If there is, it indicates an error
  // in the FrameScheduler logic (or somewhere similar), which should not have
  // scheduled another frame when there are no framebuffers available.
  FXL_CHECK(!frames_[next_frame_index_]);
  auto& frame_record = frames_[next_frame_index_] =
      NewFrameRecord(frame_timings);

  // TODO(MZ-244): See below.  What to do if rendering fails?
  frame_record->render_finished_watch.Start();
  frame_record->frame_presented_watch.Start();

  next_frame_index_ = (next_frame_index_ + 1) % kSwapchainImageCount;

  // Render the scene.
  {
    TRACE_DURATION("gfx", "DisplaySwapchain::DrawAndPresent() draw");
    draw_callback(buffer.escher_image, escher::SemaphorePtr(),
                  frame_record->render_finished_escher_semaphore);
  }

  // When the image is completely rendered, present it.
  TRACE_DURATION("gfx", "DisplaySwapchain::DrawAndPresent() present");

  display_manager_->Flip(
      display_, buffer.fb_id, frame_record->render_finished_event_id,
      frame_record->frame_presented_event_id,
      fuchsia::display::invalidId /* frame_signal_event_id */);

  display_manager_->ReleaseEvent(frame_record->render_finished_event_id);
  display_manager_->ReleaseEvent(frame_record->frame_presented_event_id);

  return true;
}

void DisplaySwapchain::OnFrameRendered(size_t frame_index,
                                       zx_time_t render_finished_time) {
  FXL_DCHECK(frame_index < kSwapchainImageCount);
  auto& record = frames_[frame_index];
  FXL_DCHECK(record);
  record->frame_timings->OnFrameRendered(record->swapchain_index,
                                         render_finished_time);
}

void DisplaySwapchain::OnFramePresented(size_t frame_index,
                                        zx_time_t vsync_time) {
  FXL_DCHECK(frame_index < kSwapchainImageCount);
  auto record = std::move(frames_[frame_index]);
  FXL_DCHECK(record);
  record->frame_timings->OnFramePresented(record->swapchain_index, vsync_time);
}

namespace {

vk::ImageUsageFlags GetFramebufferImageUsage() {
  uint32_t instance_extension_count;
  vk::Result enumerate_result =
      vk::enumerateInstanceLayerProperties(&instance_extension_count, nullptr);
  if (enumerate_result != vk::Result::eSuccess) {
    FXL_DLOG(ERROR) << "vkEnumerateInstanceLayerProperties failed: "
                    << vk::to_string(enumerate_result);
    return vk::ImageUsageFlagBits::eColorAttachment;
  }

  const std::string kGoogleImageUsageScanoutExtensionName(
      VK_GOOGLE_IMAGE_USAGE_SCANOUT_EXTENSION_NAME);
  if (instance_extension_count > 0) {
    auto instance_extensions = vk::enumerateInstanceExtensionProperties();
    if (instance_extensions.result != vk::Result::eSuccess) {
      FXL_DLOG(ERROR) << "vkEnumerateInstanceExtensionProperties failed: "
                      << vk::to_string(instance_extensions.result);
      return vk::ImageUsageFlagBits::eColorAttachment;
    }

    for (auto& extension : instance_extensions.value) {
      if (extension.extensionName == kGoogleImageUsageScanoutExtensionName) {
        return vk::ImageUsageFlagBits::eScanoutGOOGLE |
               vk::ImageUsageFlagBits::eColorAttachment;
      }
    }
  }
  FXL_DLOG(ERROR)
      << "Unable to find optimal framebuffer image usage extension ("
      << kGoogleImageUsageScanoutExtensionName << ").";
  return vk::ImageUsageFlagBits::eColorAttachment;
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

}  // namespace

}  // namespace gfx
}  // namespace scenic
