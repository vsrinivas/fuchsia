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

namespace scenic_impl {
namespace gfx {

namespace {

#define VK_CHECK_RESULT(XXX) FXL_CHECK(XXX.result == vk::Result::eSuccess)

// Helper functions

// Enumerate the formats supported for the specified surface/device, and pick a
// suitable one.
vk::Format GetDisplayImageFormat(escher::VulkanDeviceQueues* device_queues);

// Determines the necessary framebuffer image usage.
vk::ImageUsageFlags GetFramebufferImageUsage();

}  // namespace

DisplaySwapchain::DisplaySwapchain(
    Sysmem* sysmem,
    std::shared_ptr<fuchsia::hardware::display::ControllerSyncPtr> display_controller,
    std::shared_ptr<display::DisplayControllerListener> display_controller_listener,
    display::Display* display, escher::Escher* escher)
    : escher_(escher),
      sysmem_(sysmem),
      display_(display),
      display_controller_(display_controller),
      display_controller_listener_(display_controller_listener) {
  FXL_DCHECK(display);
  FXL_DCHECK(sysmem);

  if (escher_) {
    device_ = escher_->vk_device();
    queue_ = escher_->device()->vk_main_queue();
    format_ = GetDisplayImageFormat(escher->device());
    display_->Claim();

    if (!InitializeDisplayLayer()) {
      FXL_LOG(FATAL) << "Initializing display layer failed";
    }
    if (!InitializeFramebuffers(escher_->resource_recycler(), /*use_protected_memory=*/false)) {
      FXL_LOG(FATAL) << "Initializing buffers for display swapchain failed - check "
                        "whether fuchsia.sysmem.Allocator is available in this sandbox";
    }

    for (size_t i = 0; i < kSwapchainImageCount; ++i) {
      frames_.push_back(NewFrameRecord());
    }

    display_controller_listener_->SetVsyncCallback(
        fit::bind_member(this, &DisplaySwapchain::OnVsync));
    if ((*display_controller_)->EnableVsync(true) != ZX_OK) {
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

bool DisplaySwapchain::InitializeFramebuffers(escher::ResourceRecycler* resource_recycler,
                                              bool use_protected_memory) {
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
    if (format == ZX_PIXEL_FORMAT_RGB_x888 || format == ZX_PIXEL_FORMAT_ARGB_8888) {
      pixel_format = format;
      break;
    }
  }

  if (pixel_format == ZX_PIXEL_FORMAT_NONE) {
    FXL_LOG(ERROR) << "Unable to find usable pixel format.";
    return false;
  }

  SetImageConfig(primary_layer_id_, width_in_px, height_in_px, pixel_format);

  // Create all the tokens.
  fuchsia::sysmem::BufferCollectionTokenSyncPtr local_token = sysmem_->CreateBufferCollection();
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
  uint64_t display_collection_id = ImportBufferCollection(std::move(tokens[1]));
  if (!display_collection_id) {
    FXL_LOG(ERROR) << "Setting buffer collection constraints failed.";
    return false;
  }

  auto collection_closer = fbl::MakeAutoCall([this, display_collection_id]() {
    if ((*display_controller_)->ReleaseBufferCollection(display_collection_id) != ZX_OK) {
      FXL_LOG(ERROR) << "ReleaseBufferCollection failed.";
    }
  });

  // Set Vulkan buffer constraints.
  vk::ImageCreateInfo create_info;
  create_info.flags =
      use_protected_memory ? vk::ImageCreateFlagBits::eProtected : vk::ImageCreateFlags();
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
  fuchsia::sysmem::BufferCollectionConstraints constraints;
  constraints.min_buffer_count = kSwapchainImageCount;
  constraints.usage.vulkan = fuchsia::sysmem::noneUsage;
  status = sysmem_collection->SetConstraints(true, constraints);
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
  if (info.buffer_count < kSwapchainImageCount) {
    FXL_LOG(ERROR) << "Incorrect buffer collection count: " << info.buffer_count;
    return false;
  }

  for (uint32_t i = 0; i < kSwapchainImageCount; i++) {
    vk::BufferCollectionImageCreateInfoFUCHSIA collection_image_info;
    collection_image_info.collection = import_result.value;
    collection_image_info.index = i;
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
    import_info.index = i;
    vk::MemoryAllocateInfo alloc_info;
    alloc_info.setPNext(&import_info);
    alloc_info.allocationSize = memory_requirements.size;
    alloc_info.memoryTypeIndex = memory_type_index;

    auto mem_result = device_.allocateMemory(alloc_info);

    if (mem_result.result != vk::Result::eSuccess) {
      FXL_LOG(ERROR) << "vkAllocMemory failed: " << vk::to_string(mem_result.result);
      return false;
    }

    Framebuffer buffer;
    buffer.device_memory = escher::GpuMem::AdoptVkMemory(
        device_, mem_result.value, memory_requirements.size, false /* needs_mapped_ptr */);
    FXL_CHECK(buffer.device_memory);
    // Wrap the image and device memory in a escher::Image.
    escher::ImageInfo image_info;
    image_info.format = format_;
    image_info.width = width_in_px;
    image_info.height = height_in_px;
    image_info.usage = image_usage;
    image_info.memory_flags =
        use_protected_memory ? vk::MemoryPropertyFlagBits::eProtected : vk::MemoryPropertyFlags();
    // escher::NaiveImage::AdoptVkImage() binds the memory to the image.
    buffer.escher_image = escher::impl::NaiveImage::AdoptVkImage(
        resource_recycler, image_info, image_result.value, buffer.device_memory);
    if (!buffer.escher_image) {
      FXL_LOG(ERROR) << "Creating escher::EscherImage failed.";
      device_.destroyImage(image_result.value);
      return false;
    } else {
      buffer.escher_image->set_swapchain_layout(vk::ImageLayout::eColorAttachmentOptimal);
    }

    zx_status_t import_image_status = ZX_OK;
    zx_status_t transport_status = (*display_controller_)
                                       ->ImportImage(image_config_, display_collection_id, i,
                                                     &import_image_status, &buffer.fb_id);
    if (transport_status != ZX_OK || import_image_status != ZX_OK) {
      buffer.fb_id = fuchsia::hardware::display::invalidId;
      FXL_LOG(ERROR) << "Importing image failed.";
      return false;
    }

    if (use_protected_memory) {
      protected_swapchain_buffers_.push_back(std::move(buffer));
    } else {
      swapchain_buffers_.push_back(std::move(buffer));
    }
  }

  sysmem_collection->Close();

  return true;
}

DisplaySwapchain::~DisplaySwapchain() {
  if (!escher_) {
    display_->Unclaim();
    return;
  }

  // Turn off operations.
  if ((*display_controller_)->EnableVsync(false) != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to disable vsync";
  }

  display_controller_listener_->SetVsyncCallback(nullptr);

  // A FrameRecord is now stale and will no longer receive the OnFramePresented
  // callback; OnFrameDropped will clean up and make the state consistent.
  for (size_t i = 0; i < frames_.size(); ++i) {
    const size_t idx = (i + next_frame_index_) % frames_.size();
    FrameRecord* record = frames_[idx].get();
    FXL_DCHECK(record);
    if (record->frame_timings && !record->frame_timings->finalized()) {
      if (!record->rendered()) {
        // There has not been an OnFrameRendered signal. The wait will be
        // destroyed when this function returns, and will never trigger the
        // OnFrameRendered callback. Trigger it here to make the state consistent
        // in FrameTimings. Record infinite time to signal unknown render time.
        record->frame_timings->OnFrameRendered(record->swapchain_index,
                                               scheduling::FrameTimings::kTimeDropped);
      }
      if (!record->presented) {
        record->frame_timings->OnFrameDropped(record->swapchain_index);
      }
    }
    if ((*display_controller_)->ReleaseEvent(record->render_finished_event_id) != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to release render_finished event for idx=" << idx;
    }
    if ((*display_controller_)->ReleaseEvent(record->buffer_usable_event_id) != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to release buffer_usable event for idx=" << idx;
    }
  }

  display_->Unclaim();

  if ((*display_controller_)->SetDisplayLayers(display_->display_id(), {}) != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to configure display layers";
  } else {
    zx_status_t status = (*display_controller_)->ApplyConfig();
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to apply config after setting layers to empty list";
    }

    if ((*display_controller_)->DestroyLayer(primary_layer_id_) != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to destroy layer";
    }
  }

  for (auto& buffer : swapchain_buffers_) {
    if ((*display_controller_)->ReleaseImage(buffer.fb_id) != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to release image";
    }
  }
  for (auto& buffer : protected_swapchain_buffers_) {
    if ((*display_controller_)->ReleaseImage(buffer.fb_id) != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to release image";
    }
  }
}

std::unique_ptr<DisplaySwapchain::FrameRecord> DisplaySwapchain::NewFrameRecord() {
  FXL_CHECK(escher_);
  auto render_finished_escher_semaphore = escher::Semaphore::NewExportableSem(device_);
  auto buffer_usable_escher_semaphore = escher::Semaphore::NewExportableSem(device_);

  if (!render_finished_escher_semaphore || !buffer_usable_escher_semaphore) {
    FXL_LOG(ERROR) << "DisplaySwapchain::NewFrameRecord() failed to create semaphores";
    return std::unique_ptr<FrameRecord>();
  }

  zx::event render_finished_event =
      GetEventForSemaphore(escher_->device(), render_finished_escher_semaphore);
  uint64_t render_finished_event_id = ImportEvent(render_finished_event);
  zx::event buffer_usable_event =
      GetEventForSemaphore(escher_->device(), buffer_usable_escher_semaphore);
  uint64_t buffer_usable_event_id = ImportEvent(buffer_usable_event);
  FXL_DCHECK(render_finished_event_id != fuchsia::hardware::display::invalidId);
  FXL_DCHECK(buffer_usable_event_id != fuchsia::hardware::display::invalidId);

  auto record = std::make_unique<FrameRecord>();
  record->render_finished_escher_semaphore = std::move(render_finished_escher_semaphore);
  record->render_finished_event = std::move(render_finished_event);
  record->render_finished_event_id = render_finished_event_id;
  record->buffer_usable_escher_semaphore = std::move(buffer_usable_escher_semaphore);
  record->buffer_usable_event = std::move(buffer_usable_event);
  record->buffer_usable_event_id = buffer_usable_event_id;
  // At startup, a framebuffer should be ready for use.
  FXL_CHECK(record->buffer_usable_event.signal(0, ZX_EVENT_SIGNALED) == ZX_OK);
  record->presented = true;

  return record;
}

bool DisplaySwapchain::DrawAndPresentFrame(fxl::WeakPtr<scheduling::FrameTimings> frame_timings,
                                           size_t swapchain_index,
                                           const HardwareLayerAssignment& hla,
                                           zx::event frame_retired, DrawCallback draw_callback) {
  FXL_DCHECK(hla.swapchain == this);
  FXL_DCHECK(frame_timings);

  // Find the next framebuffer to render into, and other corresponding data.
  auto& buffer = use_protected_memory_ ? protected_swapchain_buffers_[next_frame_index_]
                                       : swapchain_buffers_[next_frame_index_];

  auto frame_record = frames_[next_frame_index_].get();
  FXL_CHECK(frame_record->rendered()) << "Buffer is still in use: rendering";
  FXL_CHECK(frame_record->retired()) << "Buffer is still in use: presenting";

  frame_record->swapchain_index = swapchain_index;
  frame_record->frame_timings = frame_timings;

  // The client is free to call DrawAndPresentFrame immediately after frame_retired is signaled, so
  // we chain render_finished -> buffer_usable -> frame_retired to ensure that a buffer is available.
  //
  // FrameRecord has two major states, UNUSED and PREPARED.
  // UNUSED -> PREPARED via DrawAndPresentFrame
  // Once a PREPARED frame has been rendered and either dropped or retired, the buffer is usable again.
  //  -> OnFrameRendered will begin waiting for retirement
  //  -> OnVsync will attempt to record presentation times
  //  -> OnFrameRetired must terminate all "threads" before signaling frame_retired
  auto frame_index = next_frame_index_;
  frame_record->render_finished_event.signal(ZX_EVENT_SIGNALED, 0);
  frame_record->buffer_usable_wait = std::make_unique<async::Wait>(
      frame_record->buffer_usable_event.get(), ZX_EVENT_SIGNALED, ZX_WAIT_ASYNC_TIMESTAMP,
      [this, frame_record, frame_index, frame_retired = frame_retired.release()](
          async_dispatcher_t*, async::Wait*, zx_status_t, const zx_packet_signal_t* signal) {
        OnFrameRetired(frame_record, frame_index, zx::event(frame_retired), zx::time(signal->timestamp));
      });
  frame_record->render_finished_wait = std::make_unique<async::Wait>(
      frame_record->render_finished_event.get(), escher::kFenceSignalled, ZX_WAIT_ASYNC_TIMESTAMP,
      [this, frame_record, frame_index](async_dispatcher_t* dispatcher, async::Wait* wait,
                                        zx_status_t status, const zx_packet_signal_t* signal) {
        OnFrameRendered(frame_record, frame_index, zx::time(signal->timestamp));
        // buffer_usable_event has definitely been observed by draw_callback at this point.
        frame_record->buffer_usable_event.signal(ZX_EVENT_SIGNALED, 0);
        frame_record->buffer_usable_wait->Begin(async_get_default_dispatcher());
      });

  // TODO(SCN-244): What to do if rendering fails?
  frame_record->render_finished_wait->Begin(async_get_default_dispatcher());

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
    escher::SemaphorePtr buffer_usable_escher_semaphore =
        (i == 0) ? frame_record->buffer_usable_escher_semaphore : escher::SemaphorePtr();

    // A single semaphore is sufficient to guarantee that all images have been
    // rendered, so only provide the semaphore when rendering the image for
    // the final layer.
    escher::SemaphorePtr render_finished_escher_semaphore =
        (i + 1 == num_hardware_layers) ? frame_record->render_finished_escher_semaphore
                                       : escher::SemaphorePtr();

    // TODO(SCN-1088): handle more hardware layers: the single image from
    // buffer.escher_image is not enough; we need one for each layer.
    draw_callback(frame_timings->target_presentation_time(), buffer.escher_image, hla.items[i],
                  buffer_usable_escher_semaphore, render_finished_escher_semaphore);
  }

  frame_record->presented = false;

  // When the image is completely rendered, present it.
  TRACE_DURATION("gfx", "DisplaySwapchain::DrawAndPresent() present");

  Flip(display_->display_id(), buffer.fb_id, frame_record->render_finished_event_id,
       frame_record->buffer_usable_event_id);

  FXL_CHECK(frame_record->prepared());
  return true;
}

void DisplaySwapchain::SetDisplayColorConversion(
    uint64_t display_id, fuchsia::hardware::display::ControllerSyncPtr& display_controller,
    const ColorTransform& transform) {
  // Attempt to apply color conversion.
  zx_status_t status = display_controller->SetDisplayColorConversion(
      display_id, transform.preoffsets, transform.matrix, transform.postoffsets);
  if (status != ZX_OK) {
    FXL_LOG(WARNING)
        << "DisplaySwapchain:SetDisplayColorConversion failed, controller returned status: "
        << status;
    return;
  }

  // Now check the config.
  fuchsia::hardware::display::ConfigResult result;
  std::vector<fuchsia::hardware::display::ClientCompositionOp> ops;
  display_controller->CheckConfig(/*discard=*/false, &result, &ops);

  bool client_color_conversion_required = false;
  if (result != fuchsia::hardware::display::ConfigResult::OK) {
    client_color_conversion_required = true;
  }

  for (const auto& op : ops) {
    if (op.opcode == fuchsia::hardware::display::ClientCompositionOpcode::CLIENT_COLOR_CONVERSION) {
      client_color_conversion_required = true;
      break;
    }
  }

  if (client_color_conversion_required) {
    // Clear config by calling |CheckConfig| once more with "discard" set to true.
    display_controller->CheckConfig(/*discard=*/true, &result, &ops);
    // TODO (24591): Implement scenic software fallback for color correction.
  }
}

void DisplaySwapchain::SetDisplayColorConversion(const ColorTransform& transform) {
  FXL_CHECK(display_);
  uint64_t display_id = display_->display_id();
  SetDisplayColorConversion(display_id, *display_controller_, transform);
}

void DisplaySwapchain::SetUseProtectedMemory(bool use_protected_memory) {
  if (use_protected_memory == use_protected_memory_)
    return;

  // Allocate protected memory buffers lazily and once only.
  // TODO(35785): Free this memory chunk when we no longer expect protected memory.
  if (use_protected_memory && protected_swapchain_buffers_.empty()) {
    InitializeFramebuffers(escher_->resource_recycler(), use_protected_memory);
  }

  // Mark buffers ready because we are switching from one mode to another
  for (size_t i = 0; i < frames_.size(); ++i) {
    FXL_CHECK(frames_[i]->buffer_usable_event.signal(0, ZX_EVENT_SIGNALED) == ZX_OK);
    frames_[i]->presented = true;
  }

  use_protected_memory_ = use_protected_memory;
}

bool DisplaySwapchain::InitializeDisplayLayer() {
  zx_status_t create_layer_status;
  zx_status_t transport_status =
      (*display_controller_)->CreateLayer(&create_layer_status, &primary_layer_id_);
  if (create_layer_status != ZX_OK || transport_status != ZX_OK) {
    FXL_DLOG(ERROR) << "Failed to create layer";
    return false;
  }

  zx_status_t status =
      (*display_controller_)->SetDisplayLayers(display_->display_id(), {primary_layer_id_});
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to configure display layers";
    return false;
  }
  return true;
}

void DisplaySwapchain::OnFrameRendered(FrameRecord* record, size_t frame_index,
                                       zx::time render_finished_time) {
  FXL_CHECK(record);
  if (!record->frame_timings) {
    FXL_LOG(ERROR) << "frame_timings is already gone";
    return;
  }
  uint64_t frame_number = record->frame_timings->frame_number();

  FXL_VLOG(2) << "OnFrameRendered f=" << frame_number;
  TRACE_DURATION("gfx", "DisplaySwapchain::OnFrameRendered", "frame count", frame_number,
                 "frame index", frame_index);
  TRACE_FLOW_END("gfx", "scenic_frame", frame_number);

  // It is effectively 1-indexed in the display.
  TRACE_FLOW_BEGIN("gfx", "present_image", frame_index + 1);

  record->frame_timings->OnFrameRendered(record->swapchain_index, render_finished_time);
}

void DisplaySwapchain::OnFrameRetired(FrameRecord* record, size_t frame_index, zx::event frame_retired,
                                      zx::time render_finished_time) {
  if (record->frame_timings) {
    FXL_VLOG(2) << "OnFrameRetired f=" << record->frame_timings->frame_number();
  }
  if (!record->presented) {
    FXL_VLOG(2) << "Forcing OnFrameDropped";
    record->presented = true;
    if (record->frame_timings) {
      record->frame_timings->OnFrameDropped(record->swapchain_index);
    }
    outstanding_frame_count_--;
  }
  FXL_CHECK(!record->frame_timings || record->frame_timings->finalized());
  record->frame_timings = nullptr;
  FXL_CHECK(frame_retired.signal(0, /*set_mask=*/ZX_EVENT_SIGNALED) == ZX_OK);
}

void DisplaySwapchain::OnVsync(uint64_t display_id, uint64_t timestamp,
                               std::vector<uint64_t> image_ids) {
  if (on_vsync_) {
    on_vsync_(zx::time(timestamp));
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

    // Don't double-report a frame as presented if a frame is shown twice due to the next frame
    // missing its deadline. This may also be handled by buffer_usable_wait if it fires before
    // OnVsync.
    if (!record->presented) {
      record->presented = true;

      if (record->frame_timings) {
        if (match) {
          FXL_VLOG(2) << "f=" << record->frame_timings->frame_number()
                      << " idx=" << presented_frame_idx_ << " OnFramePresented";
          record->frame_timings->OnFramePresented(record->swapchain_index, zx::time(timestamp));
        } else {
          FXL_VLOG(2) << "f=" << record->frame_timings->frame_number()
                      << " idx=" << presented_frame_idx_ << " OnFrameDropped";
          record->frame_timings->OnFrameDropped(record->swapchain_index);
        }
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
      // If record->buffer_usable_wait already fired, then it has decremented
      // outstanding_frame_count_ for us.
      if (!record->retired()) {
        outstanding_frame_count_--;
      }
    }
  }
}

uint64_t DisplaySwapchain::ImportEvent(const zx::event& event) {
  zx::event dup;
  uint64_t event_id = next_event_id_++;
  if (event.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup) != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to duplicate display controller event.";
    return fuchsia::hardware::display::invalidId;
  }

  if ((*display_controller_)->ImportEvent(std::move(dup), event_id) != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to import display controller event.";
    return fuchsia::hardware::display::invalidId;
  }
  return event_id;
}

void DisplaySwapchain::SetImageConfig(uint64_t layer_id, int32_t width, int32_t height,
                                      zx_pixel_format_t format) {
  image_config_.height = height;
  image_config_.width = width;
  image_config_.pixel_format = format;

#if defined(__x86_64__)
  // IMAGE_TYPE_X_TILED from ddk/protocol/intelgpucore.h
  image_config_.type = 1;
#elif defined(__aarch64__)
  image_config_.type = 0;
#else
  FXL_DCHECK(false) << "Display swapchain only supported on intel and ARM";
#endif

  if ((*display_controller_)->SetLayerPrimaryConfig(layer_id, image_config_) != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to set layer primary config";
  }
}

uint64_t DisplaySwapchain::ImportBufferCollection(
    fuchsia::sysmem::BufferCollectionTokenSyncPtr token) {
  uint64_t buffer_collection_id = next_buffer_collection_id_++;
  zx_status_t status;

  if ((*display_controller_)
              ->ImportBufferCollection(buffer_collection_id, std::move(token), &status) != ZX_OK ||
      status != ZX_OK) {
    FXL_LOG(ERROR) << "ImportBufferCollection failed - status: " << status;
    return 0;
  }

  if ((*display_controller_)
              ->SetBufferCollectionConstraints(buffer_collection_id, image_config_, &status) !=
          ZX_OK ||
      status != ZX_OK) {
    FXL_LOG(ERROR) << "SetBufferCollectionConstraints failed.";

    if ((*display_controller_)->ReleaseBufferCollection(buffer_collection_id) != ZX_OK) {
      FXL_LOG(ERROR) << "ReleaseBufferCollection failed.";
    }
    return 0;
  }

  return buffer_collection_id;
}

void DisplaySwapchain::Flip(uint64_t layer_id, uint64_t buffer, uint64_t render_finished_event_id,
                            uint64_t signal_event_id) {
  zx_status_t status =
      (*display_controller_)
          ->SetLayerImage(layer_id, buffer, render_finished_event_id, signal_event_id);
  // TODO(SCN-244): handle this more robustly.
  FXL_CHECK(status == ZX_OK) << "DisplaySwapchain::Flip failed";

  status = (*display_controller_)->ApplyConfig();
  // TODO(SCN-244): handle this more robustly.
  FXL_CHECK(status == ZX_OK) << "DisplaySwapchain::Flip failed";
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
