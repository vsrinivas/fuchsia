// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/swapchain/buffer_pool.h"

#include <fuchsia/sysmem/cpp/fidl.h>
#include <lib/async/default.h>
#include <lib/trace/event.h>

#include <fbl/auto_call.h>

#include "src/ui/lib/escher/escher.h"
#include "src/ui/lib/escher/flib/fence.h"
#include "src/ui/lib/escher/impl/naive_image.h"
#include "src/ui/lib/escher/util/bit_ops.h"
#include "src/ui/lib/escher/util/fuchsia_utils.h"
#include "src/ui/lib/escher/util/image_utils.h"
#include "src/ui/lib/escher/vk/chained_semaphore_generator.h"
#include "src/ui/lib/escher/vk/gpu_mem.h"
#include "src/ui/lib/escher/vk/image_layout_updater.h"
#include "src/ui/scenic/lib/display/util.h"

#define VK_CHECK_RESULT(XXX) FX_CHECK(XXX.result == vk::Result::eSuccess)

namespace scenic_impl {
namespace gfx {
namespace {

// Highest priority format first.
const vk::Format kPreferredImageFormats[] = {
    vk::Format::eR8G8B8A8Srgb,
    vk::Format::eB8G8R8A8Srgb,
};

}  // namespace

BufferPool::BufferPool(size_t count, Environment* environment, bool use_protected_memory) {
  FX_CHECK(CreateBuffers(count, environment, use_protected_memory));
}

BufferPool& BufferPool::operator=(BufferPool&& rhs) {
  buffers_ = std::move(rhs.buffers_);
  used_ = std::move(rhs.used_);
  image_config_ = rhs.image_config_;
  image_format_ = rhs.image_format_;
  return *this;
}

BufferPool::~BufferPool() { FX_CHECK(buffers_.empty()); }

BufferPool::Framebuffer* BufferPool::GetUnused() {
  for (size_t i = 0; i < buffers_.size(); ++i) {
    if (!used_[i]) {
      used_[i] = true;
      return &buffers_[i];
    }
  }
  return nullptr;
}

void BufferPool::Put(BufferPool::Framebuffer* f) {
  for (size_t i = 0; i < buffers_.size(); ++i) {
    if (&buffers_[i] == f) {
      used_[i] = false;
      return;
    }
  }
  FX_CHECK(false) << "Tried to release a buffer not owned by this pool";
}

void BufferPool::Clear(
    std::shared_ptr<fuchsia::hardware::display::ControllerSyncPtr> display_controller) {
  for (size_t i = 0; i < buffers_.size(); ++i) {
    if ((*display_controller)->ReleaseImage(buffers_[i].id) != ZX_OK) {
      FX_LOGS(ERROR) << "Failed to release image id=" << buffers_[i].id;
    }
  }
  buffers_.clear();
}

static vk::ImageUsageFlags GetFramebufferImageUsage() {
  return vk::ImageUsageFlagBits::eColorAttachment |
         // For blitting frame #.
         vk::ImageUsageFlagBits::eTransferDst;
}

static vk::Format GetDisplayImageFormat(zx_pixel_format_t pixel_format) {
  switch (pixel_format) {
    case ZX_PIXEL_FORMAT_RGB_x888:
    case ZX_PIXEL_FORMAT_ARGB_8888:
      return vk::Format::eB8G8R8A8Srgb;
    case ZX_PIXEL_FORMAT_BGR_888x:
    case ZX_PIXEL_FORMAT_ABGR_8888:
      return vk::Format::eR8G8B8A8Srgb;
  }
  FX_CHECK(false) << "Unsupported pixel format: " << pixel_format;
  return vk::Format::eUndefined;
}

// Create a number of synced tokens that can be imported into collections.
static std::vector<fuchsia::sysmem::BufferCollectionTokenSyncPtr> DuplicateToken(
    const fuchsia::sysmem::BufferCollectionTokenSyncPtr& input, uint32_t count) {
  std::vector<fuchsia::sysmem::BufferCollectionTokenSyncPtr> output;
  for (uint32_t i = 0; i < count; ++i) {
    fuchsia::sysmem::BufferCollectionTokenSyncPtr new_token;
    zx_status_t status =
        input->Duplicate(std::numeric_limits<uint32_t>::max(), new_token.NewRequest());
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "Unable to duplicate sysmem token:" << status;
      return std::vector<fuchsia::sysmem::BufferCollectionTokenSyncPtr>();
    }
    output.push_back(std::move(new_token));
  }
  zx_status_t status = input->Sync();
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Unable to sync sysmem token:" << status;
    return std::vector<fuchsia::sysmem::BufferCollectionTokenSyncPtr>();
  }
  return output;
}

bool BufferPool::CreateBuffers(size_t count, BufferPool::Environment* environment,
                               bool use_protected_memory) {
  if (count == 0) {
    return true;
  }

  FX_CHECK(environment->escher);
  FX_CHECK(buffers_.empty());
  buffers_.resize(count);
  used_.resize(count);
  vk::ImageUsageFlags image_usage = GetFramebufferImageUsage();
  image_format_ = vk::Format::eUndefined;

  const uint32_t width_in_px = environment->display->width_in_px();
  const uint32_t height_in_px = environment->display->height_in_px();

  zx_pixel_format_t pixel_format = ZX_PIXEL_FORMAT_NONE;
  for (auto preferred_format : kPreferredImageFormats) {
    for (zx_pixel_format_t format : environment->display->pixel_formats()) {
      vk::Format vk_format = GetDisplayImageFormat(format);
      if (vk_format == preferred_format) {
        pixel_format = format;
        image_format_ = vk_format;
        break;
      }
    }
    if (pixel_format != ZX_PIXEL_FORMAT_NONE) {
      break;
    }
  }

  if (pixel_format == ZX_PIXEL_FORMAT_NONE) {
    FX_LOGS(ERROR) << "Unable to find usable pixel format.";
    return false;
  }

  image_config_.height = height_in_px;
  image_config_.width = width_in_px;
  image_config_.pixel_format = pixel_format;

#if defined(__x86_64__)
  // IMAGE_TYPE_X_TILED from ddk/protocol/intelgpucore.h
  image_config_.type = 1;
#elif defined(__aarch64__)
  image_config_.type = 0;
#else
  FX_DCHECK(false) << "Display swapchain only supported on intel and ARM";
#endif

  // Create all the tokens.
  fuchsia::sysmem::BufferCollectionTokenSyncPtr local_token =
      environment->sysmem->CreateBufferCollection();
  if (!local_token) {
    FX_LOGS(ERROR) << "Sysmem tokens couldn't be allocated";
    return false;
  }
  zx_status_t status;

  auto tokens = DuplicateToken(local_token, 2);

  if (tokens.empty()) {
    FX_LOGS(ERROR) << "Sysmem tokens failed to be duped.";
    return false;
  }

  // Set display buffer constraints.
  auto display_collection_id = scenic_impl::ImportBufferCollection(
      *environment->display_controller.get(), std::move(tokens[1]), image_config_);
  if (!display_collection_id) {
    FX_LOGS(ERROR) << "Setting buffer collection constraints failed.";
    return false;
  }

  auto collection_closer = fbl::MakeAutoCall([environment, display_collection_id]() {
    if ((*environment->display_controller)->ReleaseBufferCollection(display_collection_id) !=
        ZX_OK) {
      FX_LOGS(ERROR) << "ReleaseBufferCollection failed.";
    }
  });

  // Set Vulkan buffer constraints.
  vk::ImageCreateInfo create_info;
  create_info.flags =
      use_protected_memory ? vk::ImageCreateFlagBits::eProtected : vk::ImageCreateFlags();
  create_info.imageType = vk::ImageType::e2D;
  create_info.format = image_format_;
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
  auto import_result = environment->vk_device.createBufferCollectionFUCHSIA(
      import_collection, nullptr, environment->escher->device()->dispatch_loader());
  if (import_result.result != vk::Result::eSuccess) {
    FX_LOGS(ERROR) << "VkImportBufferCollectionFUCHSIA failed: "
                   << vk::to_string(import_result.result);
    return false;
  }

  auto vulkan_collection_closer = fbl::MakeAutoCall([environment, import_result]() {
    environment->vk_device.destroyBufferCollectionFUCHSIA(
        import_result.value, nullptr, environment->escher->device()->dispatch_loader());
  });

  auto constraints_result = environment->vk_device.setBufferCollectionConstraintsFUCHSIA(
      import_result.value, create_info, environment->escher->device()->dispatch_loader());
  if (constraints_result != vk::Result::eSuccess) {
    FX_LOGS(ERROR) << "VkSetBufferCollectionConstraints failed: "
                   << vk::to_string(constraints_result);
    return false;
  }

  // Use the local collection so we can read out the error if allocation
  // fails, and to ensure everything's allocated before trying to import it
  // into another process.
  fuchsia::sysmem::BufferCollectionSyncPtr sysmem_collection =
      environment->sysmem->GetCollectionFromToken(std::move(local_token));
  if (!sysmem_collection) {
    return false;
  }
  fuchsia::sysmem::BufferCollectionConstraints constraints;
  constraints.min_buffer_count = count;
  constraints.usage.vulkan = fuchsia::sysmem::noneUsage;
  status = sysmem_collection->SetConstraints(true, constraints);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Unable to set constraints:" << status;
    return false;
  }

  fuchsia::sysmem::BufferCollectionInfo_2 info;
  zx_status_t allocation_status = ZX_OK;
  // Wait for the buffers to be allocated.
  status = sysmem_collection->WaitForBuffersAllocated(&allocation_status, &info);
  if (status != ZX_OK || allocation_status != ZX_OK) {
    FX_LOGS(ERROR) << "Waiting for buffers failed:" << status << " " << allocation_status;
    return false;
  }

  // Import the collection into a vulkan image.
  if (info.buffer_count < count) {
    FX_LOGS(ERROR) << "Incorrect buffer collection count: " << info.buffer_count;
    return false;
  }

  escher::ImageLayoutUpdater layout_updater(environment->escher->GetWeakPtr());

  for (uint32_t i = 0; i < count; ++i) {
    vk::BufferCollectionImageCreateInfoFUCHSIA collection_image_info;
    collection_image_info.collection = import_result.value;
    collection_image_info.index = i;
    create_info.setPNext(&collection_image_info);

    auto image_result = environment->vk_device.createImage(create_info);
    if (image_result.result != vk::Result::eSuccess) {
      FX_LOGS(ERROR) << "VkCreateImage failed: " << vk::to_string(image_result.result);
      return false;
    }

    auto memory_requirements =
        environment->vk_device.getImageMemoryRequirements(image_result.value);
    auto collection_properties = environment->vk_device.getBufferCollectionPropertiesFUCHSIA(
        import_result.value, environment->escher->device()->dispatch_loader());
    if (collection_properties.result != vk::Result::eSuccess) {
      FX_LOGS(ERROR) << "VkGetBufferCollectionProperties failed: "
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

    auto mem_result = environment->vk_device.allocateMemory(alloc_info);

    if (mem_result.result != vk::Result::eSuccess) {
      FX_LOGS(ERROR) << "vkAllocMemory failed: " << vk::to_string(mem_result.result);
      return false;
    }

    Framebuffer buffer;
    buffer.device_memory =
        escher::GpuMem::AdoptVkMemory(environment->vk_device, mem_result.value,
                                      memory_requirements.size, false /* needs_mapped_ptr */);
    FX_CHECK(buffer.device_memory);

    // Wrap the image and device memory in a escher::Image.
    escher::ImageInfo image_info;
    image_info.format = image_format_;
    image_info.width = width_in_px;
    image_info.height = height_in_px;
    image_info.usage = image_usage;
    image_info.memory_flags =
        use_protected_memory ? vk::MemoryPropertyFlagBits::eProtected : vk::MemoryPropertyFlags();

    // escher::NaiveImage::AdoptVkImage() binds the memory to the image.
    buffer.escher_image = escher::impl::NaiveImage::AdoptVkImage(
        environment->recycler, image_info, image_result.value, buffer.device_memory,
        create_info.initialLayout);

    if (!buffer.escher_image) {
      FX_LOGS(ERROR) << "Creating escher::EscherImage failed.";
      environment->vk_device.destroyImage(image_result.value);
      return false;
    } else {
      vk::ImageLayout kSwapchainLayout = vk::ImageLayout::eColorAttachmentOptimal;
      buffer.escher_image->set_swapchain_layout(kSwapchainLayout);
      layout_updater.ScheduleSetImageInitialLayout(buffer.escher_image, kSwapchainLayout);
    }
    zx_status_t import_image_status = ZX_OK;
    zx_status_t transport_status = (*environment->display_controller)
                                       ->ImportImage(image_config_, display_collection_id, i,
                                                     &import_image_status, &buffer.id);
    if (transport_status != ZX_OK || import_image_status != ZX_OK) {
      buffer.id = fuchsia::hardware::display::INVALID_DISP_ID;
      FX_LOGS(ERROR) << "Importing image failed.";
      return false;
    }

    buffers_[i] = std::move(buffer);
    used_[i] = false;
  }

  auto semaphore_pair = environment->escher->semaphore_chain()->TakeLastAndCreateNextSemaphore();
  layout_updater.AddWaitSemaphore(std::move(semaphore_pair.semaphore_to_wait),
                                  vk::PipelineStageFlagBits::eColorAttachmentOutput);
  layout_updater.AddSignalSemaphore(std::move(semaphore_pair.semaphore_to_signal));
  layout_updater.Submit();

  sysmem_collection->Close();

  return true;
}

}  // namespace gfx
}  // namespace scenic_impl
