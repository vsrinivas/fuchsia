// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/engine/gfx_buffer_collection_importer.h"

#include <lib/async/default.h>
#include <zircon/assert.h>

#include "src/ui/lib/escher/impl/vulkan_utils.h"
#include "src/ui/lib/escher/util/image_utils.h"
#include "src/ui/scenic/lib/gfx/engine/session.h"
#include "src/ui/scenic/lib/gfx/resources/gpu_image.h"
#include "src/ui/scenic/lib/gfx/resources/memory.h"

namespace {

// Image formats supported by Scenic in a priority order.
const vk::Format kPreferredImageFormats[] = {vk::Format::eR8G8B8A8Srgb, vk::Format::eB8G8R8A8Srgb,
                                             vk::Format::eG8B8R83Plane420Unorm,
                                             vk::Format::eG8B8R82Plane420Unorm};
}  // namespace

namespace scenic_impl {
namespace gfx {

GfxBufferCollectionImporter::GfxBufferCollectionImporter(escher::EscherWeakPtr escher)
    : dispatcher_(async_get_default_dispatcher()), escher_(escher) {}

GfxBufferCollectionImporter::~GfxBufferCollectionImporter() {
  FX_DCHECK(dispatcher_ == async_get_default_dispatcher());
  ZX_ASSERT(buffer_collection_infos_.empty());
}

bool GfxBufferCollectionImporter::ImportBufferCollection(
    allocation::GlobalBufferCollectionId collection_id,
    fuchsia::sysmem::Allocator_Sync* sysmem_allocator,
    fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token) {
  FX_DCHECK(dispatcher_ == async_get_default_dispatcher());

  if (buffer_collection_infos_.find(collection_id) != buffer_collection_infos_.end()) {
    FX_LOGS(ERROR) << __func__ << "failed, called with pre-existing collection_id " << collection_id
                   << ".";
    return false;
  }

  fuchsia::sysmem::BufferCollectionTokenSyncPtr local_token = token.BindSync();
  fuchsia::sysmem::BufferCollectionTokenSyncPtr vulkan_token;
  zx_status_t status =
      local_token->Duplicate(std::numeric_limits<uint32_t>::max(), vulkan_token.NewRequest());
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << __func__ << "failed, could not duplicate token: " << status;
    return false;
  }

  // Create sysmem BufferCollectionSyncPtr.
  fuchsia::sysmem::BufferCollectionSyncPtr buffer_collection_sync_ptr;
  {
    // Use local token to create a BufferCollection and then sync. We can trust
    // |buffer_collection_sync_ptr->Sync()| to tell us if we have a bad or malicious channel. So if
    // this call passes, then we know we have a valid BufferCollection.
    sysmem_allocator->BindSharedCollection(std::move(local_token),
                                           buffer_collection_sync_ptr.NewRequest());
    zx_status_t status = buffer_collection_sync_ptr->Sync();
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << __func__ << "failed, could not bind buffer collection: " << status;
      return false;
    }

    buffer_collection_sync_ptr->SetName(10u, "GFXBufferCollection");
    status = buffer_collection_sync_ptr->SetConstraints(
        false /* has_constraints */, fuchsia::sysmem::BufferCollectionConstraints());
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << __func__ << "failed, could not set constraints: " << status;
      return false;
    }
  }

  // Create vk::BufferCollectionFUCHSIA.
  vk::BufferCollectionFUCHSIAX vk_buffer_collection;
  {
    std::vector<vk::ImageCreateInfo> create_infos;
    for (const auto& format : kPreferredImageFormats) {
      create_infos.push_back(escher::image_utils::GetDefaultImageConstraints(format));
    }

    vk::ImageConstraintsInfoFUCHSIAX image_constraints_info;
    image_constraints_info.createInfoCount = create_infos.size();
    image_constraints_info.pCreateInfos = create_infos.data();
    image_constraints_info.pFormatConstraints = nullptr;
    image_constraints_info.pNext = nullptr;
    image_constraints_info.minBufferCount = 1;
    image_constraints_info.minBufferCountForDedicatedSlack = 0;
    image_constraints_info.minBufferCountForSharedSlack = 0;
    if (escher_->allow_protected_memory())
      image_constraints_info.flags = vk::ImageConstraintsInfoFlagBitsFUCHSIAX::eProtectedOptional;

    // Set constraints.
    vk::BufferCollectionCreateInfoFUCHSIAX buffer_collection_create_info;
    buffer_collection_create_info.collectionToken = vulkan_token.Unbind().TakeChannel().release();
    auto vk_device = escher_->vk_device();
    auto vk_loader = escher_->device()->dispatch_loader();
    auto vk_buffer_collection_result =
        vk_device.createBufferCollectionFUCHSIAX(buffer_collection_create_info, nullptr, vk_loader);
    if (vk_buffer_collection_result.result != vk::Result::eSuccess) {
      FX_LOGS(ERROR) << __func__ << " failed, could not create BufferCollectionFUCHSIA: "
                     << vk::to_string(vk_buffer_collection_result.result);
      return false;
    }
    vk_buffer_collection = vk_buffer_collection_result.value;
    auto set_contraints_result = vk_device.setBufferCollectionImageConstraintsFUCHSIAX(
        vk_buffer_collection, image_constraints_info, vk_loader);
    if (set_contraints_result != vk::Result::eSuccess) {
      FX_LOGS(ERROR) << __func__ << " failed, could not set constraints: "
                     << vk::to_string(set_contraints_result);
      return false;
    }
  }

  buffer_collection_infos_[collection_id] = {std::move(vk_buffer_collection),
                                             std::move(buffer_collection_sync_ptr)};
  return true;
}

void GfxBufferCollectionImporter::ReleaseBufferCollection(
    allocation::GlobalBufferCollectionId collection_id) {
  FX_DCHECK(dispatcher_ == async_get_default_dispatcher());

  auto itr = buffer_collection_infos_.find(collection_id);
  if (itr == buffer_collection_infos_.end()) {
    FX_LOGS(ERROR) << __func__ << " failed, collection_id " << collection_id << " not found.";
    return;
  }

  auto vk_device = escher_->vk_device();
  auto vk_loader = escher_->device()->dispatch_loader();
  vk_device.destroyBufferCollectionFUCHSIAX(itr->second.vk_buffer_collection, nullptr, vk_loader);

  zx_status_t status = itr->second.buffer_collection_sync_ptr->Close();
  FX_DCHECK(status == ZX_OK) << "failed to close buffer collection ptr, status: " << status;

  buffer_collection_infos_.erase(itr);
}

bool GfxBufferCollectionImporter::ImportBufferImage(const allocation::ImageMetadata& metadata) {
  FX_NOTREACHED();
  return false;
}

void GfxBufferCollectionImporter::ReleaseBufferImage(allocation::GlobalImageId image_id) {
  FX_NOTREACHED();
}

fxl::RefPtr<GpuImage> GfxBufferCollectionImporter::ExtractImage(
    Session* session, const allocation::ImageMetadata& metadata, ResourceId id) {
  FX_DCHECK(dispatcher_ == async_get_default_dispatcher());

  auto buffer_collection_itr = buffer_collection_infos_.find(metadata.collection_id);
  if (buffer_collection_itr == buffer_collection_infos_.end()) {
    FX_LOGS(ERROR) << __func__ << " failed, collection_id " << metadata.collection_id
                   << " not found.";
    return nullptr;
  }

  // Check if allocation is completed.
  zx_status_t allocation_status = ZX_OK;
  zx_status_t status =
      buffer_collection_itr->second.buffer_collection_sync_ptr->CheckBuffersAllocated(
          &allocation_status);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << __func__ << " failed, could not check if collection is allocated: " << status;
    return nullptr;
  }
  if (allocation_status != ZX_OK) {
    FX_LOGS(ERROR) << __func__ << " failed, collection was not allocated: " << allocation_status;
    return nullptr;
  }

  auto vk_device = escher_->vk_device();
  auto vk_loader = escher_->device()->dispatch_loader();
  const auto& vk_buffer_collection = buffer_collection_itr->second.vk_buffer_collection;

  // Grab the collection properties from Vulkan.
  auto properties_result =
      vk_device.getBufferCollectionProperties2FUCHSIAX(vk_buffer_collection, vk_loader);
  if (properties_result.result != vk::Result::eSuccess) {
    FX_LOGS(ERROR) << __func__ << " failed, could not get collection properties: "
                   << vk::to_string(properties_result.result);
    return nullptr;
  }
  auto properties = properties_result.value;

  // Check the provided index against actually allocated number of buffers.
  if (properties.bufferCount <= metadata.vmo_index) {
    FX_LOGS(ERROR) << __func__ << " failed, specified vmo index is out of bounds: " << index;
    return nullptr;
  }

  // Check if allocated buffers are backed by protected memory.
  bool is_protected =
      (escher_->vk_physical_device()
           .getMemoryProperties()
           .memoryTypes[escher::CountTrailingZeros(properties.memoryTypeBits)]
           .propertyFlags &
       vk::MemoryPropertyFlagBits::eProtected) == vk::MemoryPropertyFlagBits::eProtected;

  // Setup vk::ImageCreateInfo.
  vk::BufferCollectionImageCreateInfoFUCHSIAX collection_image_info;
  collection_image_info.collection = vk_buffer_collection;
  collection_image_info.index = metadata.vmo_index;
  vk::ImageCreateInfo create_info = escher::image_utils::GetDefaultImageConstraints(
      kPreferredImageFormats[properties.createInfoIndex]);
  create_info.setPNext(&collection_image_info);
  create_info.extent = vk::Extent3D{metadata.width, metadata.height, 1};
  if (is_protected)
    create_info.flags = vk::ImageCreateFlagBits::eProtected;

  // Create vk::Image.
  auto image_result = vk_device.createImageUnique(create_info);
  if (image_result.result != vk::Result::eSuccess) {
    FX_LOGS(ERROR) << __func__
                   << " failed, vk::CreateImage failed: " << vk::to_string(image_result.result);
    return nullptr;
  }

  // Create vk::Memory for the image.
  vk::MemoryRequirements memory_requirements;
  vk_device.getImageMemoryRequirements(*image_result.value, &memory_requirements);
  uint32_t memory_type_index =
      escher::CountTrailingZeros(memory_requirements.memoryTypeBits & properties.memoryTypeBits);
  vk::StructureChain<vk::MemoryAllocateInfo, vk::ImportMemoryBufferCollectionFUCHSIAX,
                     vk::MemoryDedicatedAllocateInfoKHR>
      alloc_info(vk::MemoryAllocateInfo()
                     .setAllocationSize(memory_requirements.size)
                     .setMemoryTypeIndex(memory_type_index),
                 vk::ImportMemoryBufferCollectionFUCHSIAX()
                     .setCollection(vk_buffer_collection)
                     .setIndex(metadata.vmo_index),
                 vk::MemoryDedicatedAllocateInfoKHR().setImage(*image_result.value));

  // Create Scenic's Memory and Image resource objects using vk objects.
  auto memory =
      Memory::New(session, 0u, alloc_info.get<vk::MemoryAllocateInfo>(), session->error_reporter());
  if (!memory) {
    FX_LOGS(ERROR) << __func__ << ": Unable to create a memory object.";
    return nullptr;
  }
  auto image = GpuImage::New(session, id, memory, create_info, image_result.value.release(),
                             session->error_reporter());
  if (!image) {
    FX_LOGS(ERROR) << __func__ << ": Unable to create an Image object.";
    return nullptr;
  }

  return image;
}

}  // namespace gfx
}  // namespace scenic_impl
