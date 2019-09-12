// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/swapchain/display_swapchain.h"

#include <fuchsia/sysmem/cpp/fidl.h>
#include <lib/async/default.h>

#include <fbl/auto_call.h>
#include <trace/event.h>

#include "src/ui/lib/escher/escher.h"
#include "src/ui/lib/escher/flib/fence.h"
#include "src/ui/lib/escher/impl/naive_image.h"
#include "src/ui/lib/escher/util/bit_ops.h"
#include "src/ui/lib/escher/util/fuchsia_utils.h"
#include "src/ui/lib/escher/util/image_utils.h"
#include "src/ui/lib/escher/vk/gpu_mem.h"
#include "src/ui/scenic/lib/gfx/displays/display.h"
#include "src/ui/scenic/lib/gfx/displays/display_manager.h"
#include "src/ui/scenic/lib/gfx/engine/frame_timings.h"
#include "src/ui/scenic/lib/gfx/sysmem.h"

namespace scenic_impl {
namespace gfx {

namespace {

#define VK_CHECK_RESULT(XXX) FXL_CHECK(XXX.result == vk::Result::eSuccess)

// Double-buffering is sufficient to pipeline CPU and GPU work, because swapchains only handle GPU
// work and presentation. This allows a swapchain with 10ms CPU work and 10ms GPU work to maintain
// 60 FPS without tearing and adding at most one frame of latency.
const uint32_t kSwapchainImageCount = 3;

// Helper functions

// Enumerate the formats supported for the specified surface/device, and pick a
// suitable one.
vk::Format GetDisplayImageFormat(escher::VulkanDeviceQueues* device_queues);

// Determines the necessary framebuffer image usage.
vk::ImageUsageFlags GetFramebufferImageUsage();

}  // namespace

DisplaySwapchain::DisplaySwapchain(Sysmem* sysmem, DisplayManager* display_manager,
                                   Display* display, escher::Escher* escher)
    : escher_(escher), sysmem_(sysmem), display_manager_(display_manager), display_(display) {
  FXL_DCHECK(display);
  FXL_DCHECK(sysmem);

  if (escher_) {
    device_ = escher_->vk_device();
    queue_ = escher_->device()->vk_main_queue();
    format_ = GetDisplayImageFormat(escher->device());
    display_->Claim();

    if (!InitializeFramebuffers(escher_->resource_recycler(), /*use_protected_memory=*/false)) {
      FXL_LOG(FATAL) << "Initializing buffers for display swapchain failed - check "
                        "whether fuchsia.sysmem.Allocator is available in this sandbox";
    }

    for (size_t i = 0; i < kSwapchainImageCount; ++i) {
      frames_.push_back(NewFrameRecord());
    }

    if (!display_manager_->EnableVsync(fit::bind_member(this, &DisplaySwapchain::OnVsync))) {
      FXL_LOG(ERROR) << "Failed to enable vsync";
    }
  } else {
    device_ = vk::Device();
    queue_ = vk::Queue();
    format_ = vk::Format::eUndefined;

    display_->Claim();

    FXL_VLOG(2) << "Using a NULL escher in DisplaySwapchain; likely in a test.";
  }
}

// Create a number of synced tokens that can be imported into collections.
static std::vector<fuchsia::sysmem::BufferCollectionTokenSyncPtr> DuplicateToken(
    const fuchsia::sysmem::BufferCollectionTokenSyncPtr& input, uint32_t count) {
  std::vector<fuchsia::sysmem::BufferCollectionTokenSyncPtr> output;
  for (uint32_t i = 0; i < count; i++) {
    fuchsia::sysmem::BufferCollectionTokenSyncPtr new_token;
    zx_status_t status =
        input->Duplicate(std::numeric_limits<uint32_t>::max(), new_token.NewRequest());
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Unable to duplicate token:" << status;
      return std::vector<fuchsia::sysmem::BufferCollectionTokenSyncPtr>();
    }
    output.push_back(std::move(new_token));
  }
  zx_status_t status = input->Sync();
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Unable to sync token:" << status;
    return std::vector<fuchsia::sysmem::BufferCollectionTokenSyncPtr>();
  }
  return output;
}

bool DisplaySwapchain::CreateBuffer(fuchsia::sysmem::BufferCollectionTokenSyncPtr local_token,
                                    escher::ResourceRecycler* resource_recycler,
                                    bool use_protected_memory, Framebuffer* buffer) {
  if (!local_token) {
    FXL_LOG(ERROR) << "Sysmem tokens couldn't be allocated";
    return false;
  }

  auto tokens = DuplicateToken(local_token, 2);

  if (tokens.empty()) {
    FXL_LOG(ERROR) << "Sysmem tokens failed to be duped.";
    return false;
  }

  // Set display buffer constraints.
  uint64_t display_collection_id = display_manager_->ImportBufferCollection(std::move(tokens[1]));
  if (!display_collection_id) {
    FXL_LOG(ERROR) << "Setting buffer collection constraints failed.";
    tokens[0]->Close();
    return false;
  }

  auto collection_closer = fbl::MakeAutoCall([this, display_collection_id]() {
    display_manager_->ReleaseBufferCollection(display_collection_id);
  });

  // Set Vulkan buffer constraints.
  vk::ImageUsageFlags image_usage = GetFramebufferImageUsage();
  vk::ImageCreateInfo create_info;
  create_info.flags =
      use_protected_memory ? vk::ImageCreateFlagBits::eProtected : vk::ImageCreateFlags();
  create_info.imageType = vk::ImageType::e2D, create_info.format = format_;
  create_info.extent = vk::Extent3D{display_->width_in_px(), display_->height_in_px(), 1};
  create_info.mipLevels = 1;
  create_info.arrayLayers = 1;
  create_info.samples = vk::SampleCountFlagBits::e1;
  create_info.tiling = vk::ImageTiling::eOptimal;
  create_info.usage = image_usage;
  create_info.sharingMode = vk::SharingMode::eExclusive;
  create_info.initialLayout = vk::ImageLayout::eUndefined;

  vk::BufferCollectionCreateInfoFUCHSIA import_collection;
  import_collection.collectionToken = tokens[0].Unbind().TakeChannel().release();
  auto import_result = device_.createBufferCollectionFUCHSIA(import_collection, nullptr,
                                                             escher_->device()->dispatch_loader());
  if (import_result.result != vk::Result::eSuccess) {
    FXL_LOG(ERROR) << "VkImportBufferCollectionFUCHSIA failed: "
                   << vk::to_string(import_result.result);
    return false;
  }

  auto vulkan_collection_closer = fbl::MakeAutoCall([this, import_result]() {
    device_.destroyBufferCollectionFUCHSIA(import_result.value, nullptr,
                                           escher_->device()->dispatch_loader());
  });

  auto constraints_result = device_.setBufferCollectionConstraintsFUCHSIA(
      import_result.value, create_info, escher_->device()->dispatch_loader());
  if (constraints_result != vk::Result::eSuccess) {
    FXL_LOG(ERROR) << "VkSetBufferCollectionConstraints failed: "
                   << vk::to_string(constraints_result);
    return false;
  }

  // Use the local collection so we can read out the error if allocation
  // fails, and to ensure everything's allocated before trying to import it
  // into another process.
  fuchsia::sysmem::BufferCollectionSyncPtr sysmem_collection =
      sysmem_->GetCollectionFromToken(std::move(local_token));
  if (!sysmem_collection) {
    return false;
  }
  fuchsia::sysmem::BufferCollectionConstraints constraints = {};
  zx_status_t status = sysmem_collection->SetConstraints(false, constraints);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Unable to set constraints:" << status;
    return false;
  }

  fuchsia::sysmem::BufferCollectionInfo_2 info;

  zx_status_t allocation_status = ZX_OK;

  // Wait for the buffers to be allocated.
  status = sysmem_collection->WaitForBuffersAllocated(&allocation_status, &info);
  if (status != ZX_OK || allocation_status != ZX_OK) {
    FXL_LOG(ERROR) << "Waiting for buffers failed:" << status << " " << allocation_status;
    return false;
  }

  // Import the collection into a vulkan image.
  if (info.buffer_count != 1) {
    FXL_LOG(ERROR) << "Incorrect buffer collection count: " << info.buffer_count;
    return false;
  }

  vk::BufferCollectionImageCreateInfoFUCHSIA collection_image_info;
  collection_image_info.collection = import_result.value;
  collection_image_info.index = 0;
  create_info.setPNext(&collection_image_info);

  auto image_result = device_.createImage(create_info);
  if (image_result.result != vk::Result::eSuccess) {
    FXL_LOG(ERROR) << "VkCreateImage failed: " << vk::to_string(image_result.result);
    return false;
  }

  auto memory_requirements = device_.getImageMemoryRequirements(image_result.value);
  auto collection_properties = device_.getBufferCollectionPropertiesFUCHSIA(
      import_result.value, escher_->device()->dispatch_loader());
  if (collection_properties.result != vk::Result::eSuccess) {
    FXL_LOG(ERROR) << "VkGetBufferCollectionProperties failed: "
                   << vk::to_string(collection_properties.result);
    return false;
  }

  uint32_t memory_type_index = escher::CountTrailingZeros(
      memory_requirements.memoryTypeBits & collection_properties.value.memoryTypeBits);
  vk::ImportMemoryBufferCollectionFUCHSIA import_info;
  import_info.collection = import_result.value;
  import_info.index = 0;
  vk::MemoryAllocateInfo alloc_info;
  alloc_info.setPNext(&import_info);
  alloc_info.allocationSize = memory_requirements.size;
  alloc_info.memoryTypeIndex = memory_type_index;

  auto mem_result = device_.allocateMemory(alloc_info);

  if (mem_result.result != vk::Result::eSuccess) {
    FXL_LOG(ERROR) << "vkAllocMemory failed: " << vk::to_string(mem_result.result);
    return false;
  }

  buffer->device_memory = escher::GpuMem::AdoptVkMemory(
      device_, mem_result.value, memory_requirements.size, false /* needs_mapped_ptr */);
  FXL_CHECK(buffer->device_memory);

  // Wrap the image and device memory in a escher::Image.
  escher::ImageInfo image_info;
  image_info.format = format_;
  image_info.width = display_->width_in_px();
  image_info.height = display_->height_in_px();
  image_info.usage = image_usage;
  image_info.memory_flags =
      use_protected_memory ? vk::MemoryPropertyFlagBits::eProtected : vk::MemoryPropertyFlags();

  // escher::NaiveImage::AdoptVkImage() binds the memory to the image.
  buffer->escher_image = escher::impl::NaiveImage::AdoptVkImage(
      resource_recycler, image_info, image_result.value, buffer->device_memory);

  if (!buffer->escher_image) {
    FXL_LOG(ERROR) << "Creating escher::EscherImage failed.";
    device_.destroyImage(image_result.value);
    return false;
  } else {
    buffer->escher_image->set_swapchain_layout(vk::ImageLayout::eColorAttachmentOptimal);
  }

  buffer->fb_id = display_manager_->ImportImage(display_collection_id, 0);
  if (buffer->fb_id == fuchsia::hardware::display::invalidId) {
    FXL_LOG(ERROR) << "Importing image failed.";
    return false;
  }

  sysmem_collection->Close();
  return true;
}

bool DisplaySwapchain::InitializeFramebuffers(escher::ResourceRecycler* resource_recycler,
                                              bool use_protected_memory) {
  FXL_CHECK(escher_);

#if !defined(__aarch64__) && !defined(__x86_64__)
  FXL_DLOG(ERROR) << "Display swapchain only supported on intel and arm";
  return false;
#endif

  const uint32_t width_in_px = display_->width_in_px();
  const uint32_t height_in_px = display_->height_in_px();
  zx_pixel_format_t pixel_format = ZX_PIXEL_FORMAT_NONE;
  for (zx_pixel_format_t format : display_->pixel_formats()) {
    // The formats are in priority order, so pick the first usable one.
    if (format == ZX_PIXEL_FORMAT_RGB_x888 || format == ZX_PIXEL_FORMAT_ARGB_8888) {
      pixel_format = format;
      break;
    }
  }

  if (pixel_format == ZX_PIXEL_FORMAT_NONE) {
    FXL_LOG(ERROR) << "Unable to find usable pixel format.";
    return false;
  }

  display_manager_->SetImageConfig(width_in_px, height_in_px, pixel_format);
  for (uint32_t i = 0; i < kSwapchainImageCount; i++) {
    // TODO(35784): Allocate all images in a single BufferCollection.
    // Create all the tokens.
    fuchsia::sysmem::BufferCollectionTokenSyncPtr local_token = sysmem_->CreateBufferCollection();
    Framebuffer buffer;
    if (!CreateBuffer(std::move(local_token), resource_recycler, use_protected_memory, &buffer)) {
      return false;
    }

    if (use_protected_memory) {
      protected_swapchain_buffers_.push_back(std::move(buffer));
    } else {
      swapchain_buffers_.push_back(std::move(buffer));
    }
  }

  return true;
}

DisplaySwapchain::~DisplaySwapchain() {
  if (!escher_) {
    display_->Unclaim();
    return;
  }

  // Turn off operations.
  display_manager_->EnableVsync(nullptr);

  // A FrameRecord is now stale and will no longer receive the OnFramePresented
  // callback; OnFrameDropped will clean up and make the state consistent.
  for (size_t i = 0; i < frames_.size(); ++i) {
    const size_t idx = (i + next_frame_index_) % frames_.size();
    FrameRecord* record = frames_[idx].get();
    if (record && !record->frame_timings->finalized()) {
      if (record->render_finished_wait->is_pending()) {
        // There has not been an OnFrameRendered signal. The wait will be
        // destroyed when this function returns, and will never trigger the
        // OnFrameRendered callback. Trigger it here to make the state consistent
        // in FrameTimings. Record infinite time to signal unknown render time.
        record->swapchain_index = record->pending_swapchain_index;
        record->frame_timings = record->pending_frame_timings;
        record->frame_timings->OnFrameRendered(record->swapchain_index, FrameTimings::kTimeDropped);
      }
      record->frame_timings->OnFrameDropped(record->swapchain_index);
    }
    display_manager_->ReleaseEvent(record->render_finished_event_id);
    display_manager_->ReleaseEvent(record->retired_event_id);
    display_manager_->ReleaseEvent(record->frame_retired_event_id);
  }

  display_->Unclaim();
  for (auto& buffer : swapchain_buffers_) {
    display_manager_->ReleaseImage(buffer.fb_id);
  }
  for (auto& buffer : protected_swapchain_buffers_) {
    display_manager_->ReleaseImage(buffer.fb_id);
  }
}

std::unique_ptr<DisplaySwapchain::FrameRecord> DisplaySwapchain::NewFrameRecord() {
  FXL_CHECK(escher_);
  auto render_finished_escher_semaphore = escher::Semaphore::NewExportableSem(device_);
  auto retired_escher_semaphore = escher::Semaphore::NewExportableSem(device_);

  if (!render_finished_escher_semaphore || !retired_escher_semaphore) {
    FXL_LOG(ERROR) << "DisplaySwapchain::NewFrameRecord() failed to create semaphores";
    return std::unique_ptr<FrameRecord>();
  }

  zx::event render_finished_event =
      GetEventForSemaphore(escher_->device(), render_finished_escher_semaphore);
  uint64_t render_finished_event_id = display_manager_->ImportEvent(render_finished_event);
  zx::event retired_event = GetEventForSemaphore(escher_->device(), retired_escher_semaphore);
  uint64_t retired_event_id = display_manager_->ImportEvent(retired_event);
  FXL_DCHECK(render_finished_event_id != fuchsia::hardware::display::invalidId);
  FXL_DCHECK(retired_event_id != fuchsia::hardware::display::invalidId);

  auto record = std::make_unique<FrameRecord>();
  record->render_finished_escher_semaphore = std::move(render_finished_escher_semaphore);
  record->render_finished_event = std::move(render_finished_event);
  record->render_finished_event_id = render_finished_event_id;
  record->retired_escher_semaphore = std::move(retired_escher_semaphore);
  record->retired_event = std::move(retired_event);
  record->retired_event_id = retired_event_id;
  // At startup, a framebuffer should be ready for use.
  FXL_CHECK(record->retired_event.signal(0, ZX_EVENT_SIGNALED) == ZX_OK);
  record->presented = true;

  return record;
}

bool DisplaySwapchain::DrawAndPresentFrame(const FrameTimingsPtr& frame_timings,
                                           size_t swapchain_index,
                                           const HardwareLayerAssignment& hla,
                                           zx::event frame_retired, DrawCallback draw_callback) {
  FXL_DCHECK(hla.swapchain == this);

  // Find the next framebuffer to render into, and other corresponding data.
  auto& buffer = use_protected_memory_ ? protected_swapchain_buffers_[next_frame_index_]
                                       : swapchain_buffers_[next_frame_index_];

  auto& frame_record = frames_[next_frame_index_];
  frame_record->pending_swapchain_index = swapchain_index;
  frame_record->pending_frame_timings = frame_timings;
  frame_record->render_finished_wait = std::make_unique<async::Wait>(
      frame_record->render_finished_event.get(), escher::kFenceSignalled, ZX_WAIT_ASYNC_TIMESTAMP,
      [this, index = next_frame_index_](async_dispatcher_t* dispatcher, async::Wait* wait,
                                        zx_status_t status, const zx_packet_signal_t* signal) {
        OnFrameRendered(index, zx::time(signal->timestamp));
      });
  uint64_t frame_retired_event_id = display_manager_->ImportEvent(frame_retired);
  FXL_CHECK(frame_retired_event_id != fuchsia::hardware::display::invalidId);
  frame_record->frame_retired_event_id = frame_retired_event_id;
  // frame_retired -> record.retired
  frame_record->frame_retired_wait = std::make_unique<async::Wait>(
      frame_retired.get(), escher::kFenceSignalled, ZX_WAIT_ASYNC_TIMESTAMP,
      [this, event = frame_retired.release(), frame_retired_event_id, index = next_frame_index_](
          async_dispatcher_t*, async::Wait*, zx_status_t, const zx_packet_signal_t*) {
        zx_handle_close(event);
        display_manager_->ReleaseEvent(frame_retired_event_id);
        FXL_CHECK(frames_[index]->retired_event.signal(0, ZX_EVENT_SIGNALED) == ZX_OK);
      });

  // TODO(SCN-244): What to do if rendering fails?
  frame_record->render_finished_wait->Begin(async_get_default_dispatcher());
  frame_record->frame_retired_wait->Begin(async_get_default_dispatcher());

  next_frame_index_ = (next_frame_index_ + 1) % kSwapchainImageCount;
  outstanding_frame_count_++;

  // Render the scene.
  size_t num_hardware_layers = hla.items.size();
  // TODO(SCN-1088): handle more hardware layers.
  FXL_DCHECK(num_hardware_layers == 1);

  // TODO(SCN-1098): we'd like to validate that the layer ID is supported
  // by the display/display-controller, but the DisplayManager API doesn't
  // currently expose it, and rather than hack in an accessor for |layer_id_|
  // we should fix this "properly", whatever that means.
  // FXL_DCHECK(hla.items[0].hardware_layer_id is supported by display);
  for (size_t i = 0; i < num_hardware_layers; ++i) {
    TRACE_DURATION("gfx", "DisplaySwapchain::DrawAndPresent() draw");

    // A single semaphore is sufficient to guarantee that all images have been
    // retired, so only provide the semaphore when acquiring the image for
    // the first layer.
    escher::SemaphorePtr retired_escher_semaphore =
        (i == 0) ? frame_record->retired_escher_semaphore : escher::SemaphorePtr();

    // A single semaphore is sufficient to guarantee that all images have been
    // rendered, so only provide the semaphore when rendering the image for
    // the final layer.
    escher::SemaphorePtr render_finished_escher_semaphore =
        (i + 1 == num_hardware_layers) ? frame_record->render_finished_escher_semaphore
                                       : escher::SemaphorePtr();

    // TODO(SCN-1088): handle more hardware layers: the single image from
    // buffer.escher_image is not enough; we need one for each layer.
    draw_callback(frame_timings->target_presentation_time(), buffer.escher_image, hla.items[i],
                  retired_escher_semaphore, render_finished_escher_semaphore);
  }

  // When the image is completely rendered, present it.
  TRACE_DURATION("gfx", "DisplaySwapchain::DrawAndPresent() present");

  frame_record->presented = false;
  display_manager_->Flip(display_, buffer.fb_id, frame_record->render_finished_event_id,
                         frame_retired_event_id);

  return true;
}

// Passes along color correction information to the display
void DisplaySwapchain::SetDisplayColorConversion(const ColorTransform& transform) {
  display_manager_->SetDisplayColorConversion(display_, transform);
}

void DisplaySwapchain::SetUseProtectedMemory(bool use_protected_memory) {
  if (use_protected_memory == use_protected_memory_)
    return;

  // Allocate protected memory buffers lazily and once only.
  // TODO(35785): Free this memory chunk when we no longer expect protected memory.
  if (use_protected_memory && protected_swapchain_buffers_.empty()) {
    InitializeFramebuffers(escher_->resource_recycler(), use_protected_memory);
  }

  use_protected_memory_ = use_protected_memory;
}

void DisplaySwapchain::OnFrameRendered(size_t frame_index, zx::time render_finished_time) {
  FXL_DCHECK(frame_index < kSwapchainImageCount);
  auto& record = frames_[frame_index];

  FXL_DCHECK(record);
  FXL_DCHECK(record->pending_frame_timings);

  size_t swapchain_index = record->pending_swapchain_index;
  uint64_t frame_number = record->pending_frame_timings->frame_number();

  TRACE_DURATION("gfx", "DisplaySwapchain::OnFrameRendered", "frame count", frame_number,
                 "frame index", frame_index);
  TRACE_FLOW_END("gfx", "scenic_frame", frame_number);

  // It is effectively 1-indexed in the display.
  TRACE_FLOW_BEGIN("gfx", "present_image", frame_index + 1);

  FXL_DCHECK(!record->frame_timings || record->frame_timings->finalized());
  record->frame_timings = record->pending_frame_timings;
  record->swapchain_index = swapchain_index;
  record->frame_timings->OnFrameRendered(swapchain_index, render_finished_time);
  // See ::OnVsync for comment about finalization.
}

void DisplaySwapchain::OnVsync(zx::time timestamp, const std::vector<uint64_t>& image_ids) {
  if (on_vsync_) {
    on_vsync_(timestamp);
  }

  if (image_ids.empty()) {
    return;
  }

  // Currently, only a single layer is ever used
  FXL_CHECK(image_ids.size() == 1);
  uint64_t image_id = image_ids[0];

  bool match = false;
  while (outstanding_frame_count_ && !match) {
    match = swapchain_buffers_[presented_frame_idx_].fb_id == image_id;
    // Check if the presented image was from |protected_swapchain_buffers_| list.
    if (!match && protected_swapchain_buffers_.size() > presented_frame_idx_) {
      match = protected_swapchain_buffers_[presented_frame_idx_].fb_id == image_id;
    }

    auto& record = frames_[presented_frame_idx_];
    // Don't double-report a frame as presented if a frame is shown twice
    // due to the next frame missing its deadline.
    if (!record->presented) {
      record->presented = true;

      // OnFrameRendered may not have fired yet, pick a valid timing record.
      FrameTimingsPtr timings;
      size_t swapchain_index;
      if (record->render_finished_wait->is_pending()) {
        timings = record->pending_frame_timings;
        swapchain_index = record->pending_swapchain_index;
      } else {
        timings = record->frame_timings;
        swapchain_index = record->swapchain_index;
      }

      if (match) {
        timings->OnFramePresented(swapchain_index, timestamp);
      } else {
        timings->OnFrameDropped(swapchain_index);
      }
    }

    // Retaining the currently displayed frame allows us to differentiate
    // between a frame being dropped and a frame being displayed twice
    // without having to look ahead in the queue, so only update the queue
    // when we know that the display controller has progressed to the next
    // frame.
    //
    // Since there is no guaranteed order between a frame being retired here
    // and OnFrameRendered() for a given frame, and since both must be called
    // for the FrameTimings to be finalized, we don't immediately destroy the
    // FrameRecord. It will eventually be replaced by DrawAndPresentFrame(),
    // when a new frame is rendered into this index.
    if (!match) {
      presented_frame_idx_ = (presented_frame_idx_ + 1) % kSwapchainImageCount;
      outstanding_frame_count_--;
    }
  }
  FXL_DCHECK(match) << "Unhandled vsync";
}

namespace {

vk::ImageUsageFlags GetFramebufferImageUsage() {
  return vk::ImageUsageFlagBits::eColorAttachment |
         // For blitting frame #.
         vk::ImageUsageFlagBits::eTransferDst;
}

vk::Format GetDisplayImageFormat(escher::VulkanDeviceQueues* device_queues) {
  return vk::Format::eB8G8R8A8Unorm;
}

}  // namespace

}  // namespace gfx
}  // namespace scenic_impl
