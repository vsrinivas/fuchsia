// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "screenshot_buffer_collection_importer.h"

#include <lib/async/default.h>
#include <lib/trace/event.h>

#include "src/ui/lib/escher/flatland/rectangle_compositor.h"
#include "src/ui/lib/escher/impl/naive_image.h"
#include "src/ui/lib/escher/impl/vulkan_utils.h"
#include "src/ui/lib/escher/resources/resource_manager.h"
#include "src/ui/lib/escher/resources/resource_recycler.h"
#include "src/ui/lib/escher/util/image_utils.h"

namespace {
// Image formats supported by Scenic in a priority order.
const vk::Format kSupportedImageFormats[] = {vk::Format::eR8G8B8A8Srgb, vk::Format::eB8G8R8A8Srgb};
}  // anonymous namespace

namespace screenshot {

ScreenshotBufferCollectionImporter::ScreenshotBufferCollectionImporter(escher::EscherWeakPtr escher)
    : dispatcher_(async_get_default_dispatcher()), escher_(escher) {}

ScreenshotBufferCollectionImporter::~ScreenshotBufferCollectionImporter() {
  auto vk_device = escher_->vk_device();
  auto vk_loader = escher_->device()->dispatch_loader();
  for (auto& [_, collection] : buffer_collection_infos_) {
    vk_device.destroyBufferCollectionFUCHSIAX(collection.vk_buffer_collection, nullptr, vk_loader);
  }
  buffer_collection_infos_.clear();
}

bool ScreenshotBufferCollectionImporter::ImportBufferCollection(
    allocation::GlobalBufferCollectionId collection_id,
    fuchsia::sysmem::Allocator_Sync* sysmem_allocator,
    fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token) {
  if (!token.is_valid()) {
    FX_LOGS(WARNING) << "ImportBufferCollection called with invalid token";
    return false;
  }

  if (buffer_collection_infos_.find(collection_id) != buffer_collection_infos_.end()) {
    FX_LOGS(WARNING) << __func__ << "failed, called with pre-existing collection_id "
                     << collection_id << ".";
    return false;
  }

  fuchsia::sysmem::BufferCollectionTokenSyncPtr local_token = token.BindSync();
  fuchsia::sysmem::BufferCollectionTokenSyncPtr vulkan_token;
  zx_status_t status =
      local_token->Duplicate(std::numeric_limits<uint32_t>::max(), vulkan_token.NewRequest());
  if (status != ZX_OK) {
    FX_LOGS(WARNING) << __func__ << "failed, could not duplicate token: " << status;
    return false;
  }

  // Create the sysmem collection.
  fuchsia::sysmem::BufferCollectionSyncPtr buffer_collection_sync_ptr;
  {
    // Use local token to create a BufferCollection and then sync. We can trust
    // |buffer_collection_sync_ptr->Sync()| to tell us if we have a bad or malicious channel. So if
    // this call passes, then we know we have a valid BufferCollection.
    sysmem_allocator->BindSharedCollection(std::move(local_token),
                                           buffer_collection_sync_ptr.NewRequest());
    zx_status_t status = buffer_collection_sync_ptr->Sync();
    if (status != ZX_OK) {
      FX_LOGS(WARNING) << __func__ << "failed, could not bind buffer collection: " << status;
      return false;
    }

    buffer_collection_sync_ptr->SetName(10u, "ScreenshotBufferCollection");
    status = buffer_collection_sync_ptr->SetConstraints(
        false /* has_constraints */, fuchsia::sysmem::BufferCollectionConstraints());
    if (status != ZX_OK) {
      FX_LOGS(WARNING) << __func__ << "failed, could not set constraints: " << status;
      return false;
    }
  }

  auto vk_loader = escher_->device()->dispatch_loader();
  auto vk_device = escher_->vk_device();

  // Create the vk collection.
  vk::BufferCollectionFUCHSIAX collection;
  {
    std::vector<vk::ImageCreateInfo> create_infos;
    for (const auto& format : kSupportedImageFormats) {
      create_infos.push_back(escher::RectangleCompositor::GetDefaultImageConstraints(
          format, escher::RectangleCompositor::kRenderTargetUsageFlags));
    }

    vk::ImageConstraintsInfoFUCHSIAX image_constraints_info;
    image_constraints_info.createInfoCount = create_infos.size();
    image_constraints_info.pCreateInfos = create_infos.data();
    image_constraints_info.pFormatConstraints = nullptr;
    image_constraints_info.pNext = nullptr;
    image_constraints_info.minBufferCount = 1;
    image_constraints_info.minBufferCountForDedicatedSlack = 0;
    image_constraints_info.minBufferCountForSharedSlack = 0;

    // While protected memory does not allow readback, clients can still render into protected
    // buffers. This is because clients can take a screenshot rendered into protected memory and
    // then display that screenshot image in their protected context.
    if (escher_->allow_protected_memory())
      image_constraints_info.flags = vk::ImageConstraintsInfoFlagBitsFUCHSIAX::eProtectedOptional;

    vk::BufferCollectionCreateInfoFUCHSIAX buffer_collection_create_info;
    buffer_collection_create_info.collectionToken = vulkan_token.Unbind().TakeChannel().release();

    collection = escher::ESCHER_CHECKED_VK_RESULT(vk_device.createBufferCollectionFUCHSIAX(
        buffer_collection_create_info, nullptr, vk_loader));

    auto vk_result = vk_device.setBufferCollectionImageConstraintsFUCHSIAX(
        collection, image_constraints_info, vk_loader);
    if (vk_result != vk::Result::eSuccess) {
      FX_LOGS(WARNING) << __func__
                       << " failed, could not set constraints: " << vk::to_string(vk_result);
      return false;
    }
  }

  buffer_collection_infos_[collection_id] = {std::move(collection),
                                             std::move(buffer_collection_sync_ptr)};

  return true;
}

void ScreenshotBufferCollectionImporter::ReleaseBufferCollection(
    allocation::GlobalBufferCollectionId collection_id) {
  auto collection_itr = buffer_collection_infos_.find(collection_id);

  // If the collection is not in the map, then there's nothing to do.
  if (collection_itr == buffer_collection_infos_.end()) {
    FX_LOGS(WARNING) << "Attempting to release a non-existent buffer collection.";
    return;
  }

  auto vk_device = escher_->vk_device();
  auto vk_loader = escher_->device()->dispatch_loader();
  vk_device.destroyBufferCollectionFUCHSIAX(collection_itr->second.vk_buffer_collection, nullptr,
                                            vk_loader);
  buffer_collection_infos_.erase(collection_id);
}

bool ScreenshotBufferCollectionImporter::ImportBufferImage(
    const allocation::ImageMetadata& metadata) {
  // The metadata can't have an invalid collection id.
  if (metadata.collection_id == allocation::kInvalidId) {
    FX_LOGS(WARNING) << "Image has invalid collection id.";
    return false;
  }

  // The metadata can't have an invalid identifier.
  if (metadata.identifier == allocation::kInvalidImageId) {
    FX_LOGS(WARNING) << "Image has invalid identifier.";
    return false;
  }

  // Check for valid dimensions.
  if (metadata.width == 0 || metadata.height == 0) {
    FX_LOGS(WARNING) << "Image has invalid dimensions: "
                     << "(" << metadata.width << ", " << metadata.height << ").";
    return false;
  }

  // Make sure that the collection that will back this image's memory
  // is actually registered.
  auto collection_itr = buffer_collection_infos_.find(metadata.collection_id);
  if (collection_itr == buffer_collection_infos_.end()) {
    FX_LOGS(WARNING) << "Collection with id " << metadata.collection_id << " does not exist.";
    return false;
  }

  // Check to see if the buffers are allocated and return false if not.
  zx_status_t allocation_status = ZX_OK;
  zx_status_t status =
      collection_itr->second.buffer_collection_sync_ptr->CheckBuffersAllocated(&allocation_status);
  if (status != ZX_OK || allocation_status != ZX_OK) {
    FX_LOGS(WARNING) << "Collection was not allocated.";
    return false;
  }

  auto image = ExtractImage(metadata, collection_itr->second.vk_buffer_collection,
                            escher::RectangleCompositor::kRenderTargetUsageFlags);
  if (!image) {
    FX_LOGS(ERROR) << "Could not extract render target.";
    return false;
  }

  image->set_swapchain_layout(vk::ImageLayout::eColorAttachmentOptimal);

  return true;
}

void ScreenshotBufferCollectionImporter::ReleaseBufferImage(allocation::GlobalImageId image_id) {}

escher::ImagePtr ScreenshotBufferCollectionImporter::ExtractImage(
    const allocation::ImageMetadata& metadata, vk::BufferCollectionFUCHSIAX collection,
    vk::ImageUsageFlags usage) {
  TRACE_DURATION("gfx", "ScreenshotBufferCollection::ExtractImage");
  auto vk_device = escher_->vk_device();
  auto vk_loader = escher_->device()->dispatch_loader();

  // Grab the collection Properties from Vulkan.
  auto properties = escher::ESCHER_CHECKED_VK_RESULT(
      vk_device.getBufferCollectionProperties2FUCHSIAX(collection, vk_loader));

  // Check the provided index against actually allocated number of buffers.
  if (properties.bufferCount <= metadata.vmo_index) {
    FX_LOGS(WARNING) << "Specified vmo index is out of bounds: " << metadata.vmo_index;
    return nullptr;
  }

  // Check if allocated buffers are backed by protected memory.
  bool is_protected =
      (escher_->vk_physical_device()
           .getMemoryProperties()
           .memoryTypes[escher::CountTrailingZeros(properties.memoryTypeBits)]
           .propertyFlags &
       vk::MemoryPropertyFlagBits::eProtected) == vk::MemoryPropertyFlagBits::eProtected;

  // Setup the create info Fuchsia extension.
  vk::BufferCollectionImageCreateInfoFUCHSIAX collection_image_info;
  collection_image_info.collection = collection;
  collection_image_info.index = metadata.vmo_index;

  // Setup the create info.
  FX_DCHECK(properties.createInfoIndex < std::size(kSupportedImageFormats));
  auto pixel_format = kSupportedImageFormats[properties.createInfoIndex];
  vk::ImageCreateInfo create_info =
      escher::RectangleCompositor::GetDefaultImageConstraints(pixel_format, usage);
  create_info.extent = vk::Extent3D{metadata.width, metadata.height, 1};
  create_info.setPNext(&collection_image_info);
  if (is_protected) {
    create_info.flags = vk::ImageCreateFlagBits::eProtected;
  }

  // Create the VK Image, return nullptr if this fails.
  auto image_result = vk_device.createImage(create_info);
  if (image_result.result != vk::Result::eSuccess) {
    FX_LOGS(ERROR) << "VkCreateImage failed: " << vk::to_string(image_result.result);
    return nullptr;
  }

  // Now we have to allocate VK memory for the image. This memory is going to come from
  // the imported buffer collection's vmo.
  auto memory_requirements = vk_device.getImageMemoryRequirements(image_result.value);
  uint32_t memory_type_index =
      escher::CountTrailingZeros(memory_requirements.memoryTypeBits & properties.memoryTypeBits);
  vk::StructureChain<vk::MemoryAllocateInfo, vk::ImportMemoryBufferCollectionFUCHSIAX,
                     vk::MemoryDedicatedAllocateInfoKHR>
      alloc_info(vk::MemoryAllocateInfo()
                     .setAllocationSize(memory_requirements.size)
                     .setMemoryTypeIndex(memory_type_index),
                 vk::ImportMemoryBufferCollectionFUCHSIAX()
                     .setCollection(collection)
                     .setIndex(metadata.vmo_index),
                 vk::MemoryDedicatedAllocateInfoKHR().setImage(image_result.value));

  vk::DeviceMemory memory = nullptr;
  vk::Result err =
      vk_device.allocateMemory(&alloc_info.get<vk::MemoryAllocateInfo>(), nullptr, &memory);
  if (err != vk::Result::eSuccess) {
    FX_LOGS(ERROR) << "Could not successfully allocate memory.";
    return nullptr;
  }

  // Have escher manager the memory since this is the required format for creating
  // an Escher image. Also we can now check if the total memory size is great enough
  // for the image memory requirements. If it's not big enough, the client likely
  // requested an image size that is larger than the maximum image size allowed by
  // the sysmem collection constraints.
  auto gpu_mem =
      escher::GpuMem::AdoptVkMemory(vk_device, vk::DeviceMemory(memory), memory_requirements.size,
                                    /*needs_mapped_ptr*/ false);
  if (memory_requirements.size > gpu_mem->size()) {
    FX_LOGS(ERROR) << "Memory requirements for image exceed available memory: "
                   << memory_requirements.size << " " << gpu_mem->size();
    return nullptr;
  }

  // Create and return an escher image.
  escher::ImageInfo escher_image_info;
  escher_image_info.format = create_info.format;
  escher_image_info.width = create_info.extent.width;
  escher_image_info.height = create_info.extent.height;
  escher_image_info.usage = create_info.usage;
  escher_image_info.memory_flags = vk::MemoryPropertyFlagBits::eDeviceLocal;
  if (create_info.flags & vk::ImageCreateFlagBits::eProtected) {
    escher_image_info.memory_flags = vk::MemoryPropertyFlagBits::eProtected;
  }
  escher_image_info.is_external = true;
  return escher::impl::NaiveImage::AdoptVkImage(escher_->resource_recycler(), escher_image_info,
                                                image_result.value, std::move(gpu_mem),
                                                create_info.initialLayout);
}

}  // namespace screenshot
