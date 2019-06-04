// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/swapchain/display_swapchain.h"

#include <fbl/auto_call.h>
#include <trace/event.h>

#include "fuchsia/sysmem/cpp/fidl.h"
#include "garnet/lib/ui/gfx/displays/display.h"
#include "garnet/lib/ui/gfx/displays/display_manager.h"
#include "garnet/lib/ui/gfx/engine/frame_timings.h"
#include "src/ui/lib/escher/escher.h"
#include "src/ui/lib/escher/flib/fence.h"
#include "src/ui/lib/escher/impl/naive_image.h"
#include "src/ui/lib/escher/util/bit_ops.h"
#include "src/ui/lib/escher/util/fuchsia_utils.h"
#include "src/ui/lib/escher/util/image_utils.h"
#include "src/ui/lib/escher/vk/gpu_mem.h"

namespace scenic_impl {
namespace gfx {

namespace {

#define VK_CHECK_RESULT(XXX) FXL_CHECK(XXX.result == vk::Result::eSuccess)

// TODO(SCN-400): Don't triple buffer.  This is done to avoid "tearing", but it
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

// Determines the necessary framebuffer image usage.
vk::ImageUsageFlags GetFramebufferImageUsage();

}  // namespace

DisplaySwapchain::DisplaySwapchain(DisplayManager* display_manager,
                                   Display* display,
                                   EventTimestamper* timestamper,
                                   escher::Escher* escher)
    : escher_(escher),
      display_manager_(display_manager),
      display_(display),
      timestamper_(timestamper) {
  FXL_DCHECK(display);
  FXL_DCHECK(timestamper);

  if (escher_) {
    device_ = escher_->vk_device();
    queue_ = escher_->device()->vk_main_queue();
    format_ = GetDisplayImageFormat(escher->device());
    display_->Claim();
    frames_.resize(kSwapchainImageCount);

    if (!InitializeFramebuffers(escher_->resource_recycler())) {
      FXL_LOG(FATAL)
          << "Initializing buffers for display swapchain failed - check "
             "whether fuchsia.sysmem.Allocator is available in this sandbox";
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
static std::vector<fuchsia::sysmem::BufferCollectionTokenSyncPtr>
DuplicateToken(const fuchsia::sysmem::BufferCollectionTokenSyncPtr& input,
               uint32_t count) {
  std::vector<fuchsia::sysmem::BufferCollectionTokenSyncPtr> output;
  for (uint32_t i = 0; i < count; i++) {
    fuchsia::sysmem::BufferCollectionTokenSyncPtr new_token;
    zx_status_t status = input->Duplicate(std::numeric_limits<uint32_t>::max(),
                                          new_token.NewRequest());
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

bool DisplaySwapchain::InitializeFramebuffers(
    escher::ResourceRecycler* resource_recycler) {
  FXL_CHECK(escher_);
  vk::ImageUsageFlags image_usage = GetFramebufferImageUsage();

#if !defined(__aarch64__) && !defined(__x86_64__)
  FXL_DLOG(ERROR) << "Display swapchain only supported on intel and arm";
  return false;
#endif

  const uint32_t width_in_px = display_->width_in_px();
  const uint32_t height_in_px = display_->height_in_px();
  zx_pixel_format_t pixel_format = ZX_PIXEL_FORMAT_NONE;
  for (zx_pixel_format_t format : display_->pixel_formats()) {
    // The formats are in priority order, so pick the first usable one.
    if (format == ZX_PIXEL_FORMAT_RGB_x888 ||
        format == ZX_PIXEL_FORMAT_ARGB_8888) {
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
    // Create all the tokens.
    fuchsia::sysmem::BufferCollectionTokenSyncPtr local_token =
        display_manager_->CreateBufferCollection();
    if (!local_token) {
      FXL_LOG(ERROR) << "Sysmem tokens couldn't be allocated";
      return false;
    }
    zx_status_t status;

    auto tokens = DuplicateToken(local_token, 2);

    if (tokens.empty()) {
      FXL_LOG(ERROR) << "Sysmem tokens failed to be duped.";
      return false;
    }

    // Set display buffer constraints.
    uint64_t display_collection_id =
        display_manager_->ImportBufferCollection(std::move(tokens[1]));
    if (!display_collection_id) {
      FXL_LOG(ERROR) << "Setting buffer collection constraints failed.";
      return false;
    }

    auto collection_closer = fbl::MakeAutoCall([this, display_collection_id]() {
      display_manager_->ReleaseBufferCollection(display_collection_id);
    });

    // Set Vulkan buffer constraints.
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

    vk::BufferCollectionCreateInfoFUCHSIA import_collection;
    import_collection.collectionToken =
        tokens[0].Unbind().TakeChannel().release();
    auto import_result = device_.createBufferCollectionFUCHSIA(
        import_collection, nullptr, escher_->device()->dispatch_loader());
    if (import_result.result != vk::Result::eSuccess) {
      FXL_LOG(ERROR) << "VkImportBufferCollectionFUCHSIA failed: "
                     << vk::to_string(import_result.result);
      return false;
    }

    auto vulkan_collection_closer = fbl::MakeAutoCall([this, import_result]() {
      device_.destroyBufferCollectionFUCHSIA(
          import_result.value, nullptr, escher_->device()->dispatch_loader());
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
        display_manager_->GetCollectionFromToken(std::move(local_token));
    if (!sysmem_collection) {
      return false;
    }
    fuchsia::sysmem::BufferCollectionConstraints constraints = {};
    status = sysmem_collection->SetConstraints(false, constraints);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Unable to set constraints:" << status;
      return false;
    }

    fuchsia::sysmem::BufferCollectionInfo_2 info;

    zx_status_t allocation_status = ZX_OK;

    // Wait for the buffers to be allocated.
    status =
        sysmem_collection->WaitForBuffersAllocated(&allocation_status, &info);
    if (status != ZX_OK || allocation_status != ZX_OK) {
      FXL_LOG(ERROR) << "Waiting for buffers failed:" << status << " "
                     << allocation_status;
      return false;
    }

    // Import the collection into a vulkan image.
    if (info.buffer_count != 1) {
      FXL_LOG(ERROR) << "Incorrect buffer collection count: "
                     << info.buffer_count;
      return false;
    }

    vk::BufferCollectionImageCreateInfoFUCHSIA collection_image_info;
    collection_image_info.collection = import_result.value;
    collection_image_info.index = 0;
    create_info.setPNext(&collection_image_info);

    auto image_result = device_.createImage(create_info);
    if (image_result.result != vk::Result::eSuccess) {
      FXL_LOG(ERROR) << "VkCreateImage failed: "
                     << vk::to_string(image_result.result);
      return false;
    }

    auto memory_requirements =
        device_.getImageMemoryRequirements(image_result.value);
    auto collection_properties = device_.getBufferCollectionPropertiesFUCHSIA(
        import_result.value, escher_->device()->dispatch_loader());
    if (collection_properties.result != vk::Result::eSuccess) {
      FXL_LOG(ERROR) << "VkGetBufferCollectionProperties failed: "
                     << vk::to_string(collection_properties.result);
      return false;
    }

    uint32_t memory_type_index =
        escher::CountTrailingZeros(memory_requirements.memoryTypeBits &
                                   collection_properties.value.memoryTypeBits);
    vk::ImportMemoryBufferCollectionFUCHSIA import_info;
    import_info.collection = import_result.value;
    import_info.index = 0;
    vk::MemoryAllocateInfo alloc_info;
    alloc_info.setPNext(&import_info);
    alloc_info.allocationSize = memory_requirements.size;
    alloc_info.memoryTypeIndex = memory_type_index;

    auto mem_result = device_.allocateMemory(alloc_info);

    if (mem_result.result != vk::Result::eSuccess) {
      FXL_LOG(ERROR) << "vkAllocMemory failed: "
                     << vk::to_string(mem_result.result);
      return false;
    }

    Framebuffer buffer;
    buffer.device_memory = escher::GpuMem::AdoptVkMemory(
        device_, mem_result.value, memory_requirements.size,
        false /* needs_mapped_ptr */);
    FXL_CHECK(buffer.device_memory);

    // Wrap the image and device memory in a escher::Image.
    escher::ImageInfo image_info;
    image_info.format = format_;
    image_info.width = width_in_px;
    image_info.height = height_in_px;
    image_info.usage = image_usage;

    // escher::NaiveImage::AdoptVkImage() binds the memory to the image.
    buffer.escher_image = escher::impl::NaiveImage::AdoptVkImage(
        resource_recycler, image_info, image_result.value,
        buffer.device_memory);

    if (!buffer.escher_image) {
      FXL_LOG(ERROR) << "Creating escher::EscherImage failed.";
      device_.destroyImage(image_result.value);
      return false;
    } else {
      buffer.escher_image->set_swapchain_layout(
          vk::ImageLayout::eColorAttachmentOptimal);
    }

    buffer.fb_id = display_manager_->ImportImage(display_collection_id, 0);
    if (buffer.fb_id == fuchsia::hardware::display::invalidId) {
      FXL_LOG(ERROR) << "Importing image failed.";
      return false;
    }

    sysmem_collection->Close();

    swapchain_buffers_.push_back(std::move(buffer));
  }

  if (!display_manager_->EnableVsync(
          fit::bind_member(this, &DisplaySwapchain::OnVsync))) {
    FXL_LOG(ERROR) << "Failed to enable vsync";
    return false;
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
      if (record->render_finished_watch.IsWatching()) {
        // There has not been an OnFrameRendered signal. The watch will be
        // destroyed when this function returns, and will never trigger the
        // OnFrameRendered callback.Trigger it here to make the state consistent
        // in FrameTimings. Record infinite time to signal unknown render time.
        record->frame_timings->OnFrameRendered(record->swapchain_index,
                                               FrameTimings::kTimeDropped);
      }
      record->frame_timings->OnFrameDropped(record->swapchain_index);
    }
  }

  display_->Unclaim();
  for (auto& buffer : swapchain_buffers_) {
    display_manager_->ReleaseImage(buffer.fb_id);
  }
}

std::unique_ptr<DisplaySwapchain::FrameRecord> DisplaySwapchain::NewFrameRecord(
    const FrameTimingsPtr& frame_timings) {
  FXL_DCHECK(frame_timings);
  FXL_CHECK(escher_);
  auto render_finished_escher_semaphore =
      escher::Semaphore::NewExportableSem(device_);

  zx::event render_finished_event =
      GetEventForSemaphore(escher_->device(), render_finished_escher_semaphore);
  uint64_t render_finished_event_id =
      display_manager_->ImportEvent(render_finished_event);

  if (!render_finished_escher_semaphore ||
      render_finished_event_id == fuchsia::hardware::display::invalidId) {
    FXL_LOG(ERROR)
        << "DisplaySwapchain::NewFrameRecord() failed to create semaphores";
    return std::unique_ptr<FrameRecord>();
  }

  zx::event retired_event;
  zx_status_t status = zx::event::create(0, &retired_event);
  if (status != ZX_OK) {
    FXL_LOG(ERROR)
        << "DisplaySwapchain::NewFrameRecord() failed to create retired event";
    return std::unique_ptr<FrameRecord>();
  }

  uint64_t retired_event_id = display_manager_->ImportEvent(retired_event);
  if (retired_event_id == fuchsia::hardware::display::invalidId) {
    FXL_LOG(ERROR)
        << "DisplaySwapchain::NewFrameRecord() failed to import retired event";
    return std::unique_ptr<FrameRecord>();
  }

  auto record = std::make_unique<FrameRecord>();
  record->frame_timings = frame_timings;
  record->swapchain_index = frame_timings->RegisterSwapchain();
  record->render_finished_escher_semaphore =
      std::move(render_finished_escher_semaphore);
  record->render_finished_event_id = render_finished_event_id;
  record->retired_event = std::move(retired_event);
  record->retired_event_id = retired_event_id;

  record->render_finished_watch = EventTimestamper::Watch(
      timestamper_, std::move(render_finished_event), escher::kFenceSignalled,
      [this, index = next_frame_index_](zx_time_t timestamp) {
        OnFrameRendered(index, timestamp);
      });

  return record;
}

bool DisplaySwapchain::DrawAndPresentFrame(const FrameTimingsPtr& frame_timings,
                                           const HardwareLayerAssignment& hla,
                                           DrawCallback draw_callback) {
  FXL_DCHECK(hla.swapchain == this);

  // Find the next framebuffer to render into, and other corresponding data.
  auto& buffer = swapchain_buffers_[next_frame_index_];

  // Create a record that can be used to notify |frame_timings| (and hence
  // ultimately the FrameScheduler) that the frame has been presented.
  //
  // There must not already exist a pending record.  If there is, it indicates
  // an error in the FrameScheduler logic (or somewhere similar), which should
  // not have scheduled another frame when there are no framebuffers available.
  if (frames_[next_frame_index_]) {
    FXL_CHECK(frames_[next_frame_index_]->frame_timings->finalized());
    if (frames_[next_frame_index_]->retired_event.wait_one(
            ZX_EVENT_SIGNALED, zx::time(), nullptr) != ZX_OK) {
      FXL_LOG(WARNING) << "DisplaySwapchain::DrawAndPresentFrame rendering "
                          "into in-use backbuffer";
    }
  }

  auto& frame_record = frames_[next_frame_index_] =
      NewFrameRecord(frame_timings);

  // TODO(SCN-244): See below.  What to do if rendering fails?
  frame_record->render_finished_watch.Start();

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
    // rendered, so only provide the semaphore when rendering the image for
    // the final layer.
    escher::SemaphorePtr render_finished_escher_semaphore =
        (i + 1 == num_hardware_layers)
            ? frame_record->render_finished_escher_semaphore
            : escher::SemaphorePtr();
    // TODO(SCN-1088): handle more hardware layers: the single image from
    // buffer.escher_image is not enough; we need one for each layer.
    draw_callback(frame_timings->target_presentation_time(),
                  buffer.escher_image, hla.items[i], escher::SemaphorePtr(),
                  render_finished_escher_semaphore);
  }

  // When the image is completely rendered, present it.
  TRACE_DURATION("gfx", "DisplaySwapchain::DrawAndPresent() present");

  display_manager_->Flip(display_, buffer.fb_id,
                         frame_record->render_finished_event_id,
                         frame_record->retired_event_id);

  display_manager_->ReleaseEvent(frame_record->render_finished_event_id);
  display_manager_->ReleaseEvent(frame_record->retired_event_id);

  return true;
}

// Passes along color correction information to the display
void DisplaySwapchain::SetDisplayColorConversion(
    const ColorTransform& transform) {
  display_manager_->SetDisplayColorConversion(display_, transform);
}

void DisplaySwapchain::OnFrameRendered(size_t frame_index,
                                       zx_time_t render_finished_time) {
  FXL_DCHECK(frame_index < kSwapchainImageCount);
  auto& record = frames_[frame_index];

  uint64_t frame_number = record->frame_timings->frame_number();

  TRACE_DURATION("gfx", "DisplaySwapchain::OnFrameRendered",
          "frame count", frame_number,
          "frame index", frame_index);
  TRACE_FLOW_END("gfx", "scenic_frame", frame_number);

  // It is effectively 1-indexed in the display.
  TRACE_FLOW_BEGIN("gfx", "present_image", frame_index + 1);

  FXL_DCHECK(record);
  record->frame_timings->OnFrameRendered(record->swapchain_index,
                                         render_finished_time);
  // See ::OnVsync for comment about finalization.
}

void DisplaySwapchain::OnVsync(zx_time_t timestamp,
                               const std::vector<uint64_t>& image_ids) {
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
    auto& buf = swapchain_buffers_[presented_frame_idx_];
    auto& record = frames_[presented_frame_idx_];
    match = buf.fb_id == image_id;

    // Don't double-report a frame as presented if a frame is shown twice
    // due to the next frame missing its deadline.
    if (!record->presented) {
      record->presented = true;

      if (match) {
        record->frame_timings->OnFramePresented(record->swapchain_index,
                                                timestamp);
      } else {
        record->frame_timings->OnFrameDropped(record->swapchain_index);
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
