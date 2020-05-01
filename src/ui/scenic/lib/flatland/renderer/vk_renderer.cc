// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/renderer/vk_renderer.h"

#include "src/ui/lib/escher/escher.h"
#include "src/ui/lib/escher/impl/vulkan_utils.h"
#include "src/ui/lib/escher/util/image_utils.h"

namespace flatland {

VkRenderer::VkRenderer(escher::EscherWeakPtr escher)
    : escher_(escher), compositor_(escher::RectangleCompositor::New(escher)) {}

VkRenderer::~VkRenderer() {
  auto vk_device = escher_->vk_device();
  auto vk_loader = escher_->device()->dispatch_loader();
  for (auto& [_, vk_collection] : vk_collection_map_) {
    vk_device.destroyBufferCollectionFUCHSIA(vk_collection, nullptr, vk_loader);
  }
}

GlobalBufferCollectionId VkRenderer::RegisterBufferCollection(
    fuchsia::sysmem::Allocator_Sync* sysmem_allocator,
    fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token) {
  auto vk_device = escher_->vk_device();
  auto vk_loader = escher_->device()->dispatch_loader();
  FX_DCHECK(vk_device);

  // Check for a null token here before we try to duplicate it to get the
  // vulkan token.
  if (!token.is_valid()) {
    FX_LOGS(ERROR) << "Token is invalid.";
    return kInvalidId;
  }

  // Returns minimal image contraints with the specified vk::Format.
  auto vk_constraints =
      escher::RectangleCompositor::GetDefaultImageConstraints(vk::Format::eB8G8R8A8Unorm);

  // Create a duped vulkan token.
  fuchsia::sysmem::BufferCollectionTokenSyncPtr vulkan_token;
  {
    // TODO(51213): See if this can become asynchronous.
    fuchsia::sysmem::BufferCollectionTokenSyncPtr sync_token = token.BindSync();
    zx_status_t status =
        sync_token->Duplicate(std::numeric_limits<uint32_t>::max(), vulkan_token.NewRequest());
    FX_DCHECK(status == ZX_OK);

    // Reassign the channel to the non-sync interface handle.
    token = sync_token.Unbind();
  }

  // Create the sysmem buffer collection. We do this before creating the vulkan collection below,
  // since New() checks if the incoming token is of the wrong type/malicious.
  auto result = BufferCollectionInfo::New(sysmem_allocator, std::move(token));
  if (result.is_error()) {
    FX_LOGS(ERROR) << "Unable to register collection.";
    return kInvalidId;
  }

  // Create the vk_collection and set its constraints.
  vk::BufferCollectionFUCHSIA vk_collection;
  {
    vk::BufferCollectionCreateInfoFUCHSIA buffer_collection_create_info;
    buffer_collection_create_info.collectionToken = vulkan_token.Unbind().TakeChannel().release();
    vk_collection = escher::ESCHER_CHECKED_VK_RESULT(
        vk_device.createBufferCollectionFUCHSIA(buffer_collection_create_info, nullptr, vk_loader));
    auto vk_result =
        vk_device.setBufferCollectionConstraintsFUCHSIA(vk_collection, vk_constraints, vk_loader);
    FX_DCHECK(vk_result == vk::Result::eSuccess);
  }

  // Atomically increment the id generator and create a new identifier for the
  // current buffer collection.
  GlobalBufferCollectionId identifier = id_generator_++;

  // Multiple threads may be attempting to read/write from |collection_map_| so we
  // lock this function here.
  // TODO(44335): Convert this to a lock-free structure.
  std::unique_lock<std::mutex> lock(lock_);
  collection_map_[identifier] = std::move(result.value());
  vk_collection_map_[identifier] = std::move(vk_collection);
  return identifier;
}

std::optional<BufferCollectionMetadata> VkRenderer::Validate(
    GlobalBufferCollectionId collection_id) {
  // TODO(44335): Convert this to a lock-free structure. This is trickier than in the other
  // cases for this class since we are mutating the buffer collection in this call. So we can
  // only convert this to a lock free structure if the elements in the map are changed to be values
  // only, or if we can guarantee that mutations on the elements only occur in a single thread.
  std::unique_lock<std::mutex> lock(lock_);
  auto collection_itr = collection_map_.find(collection_id);

  // If the collection is not in the map, then it can't be validated.
  if (collection_itr == collection_map_.end()) {
    return std::nullopt;
  }

  // If there is already metadata, we can just return it instead of checking the allocation
  // status again. Once a buffer is allocated it won't be stop being allocated.
  auto metadata_itr = collection_metadata_map_.find(collection_id);
  if (metadata_itr != collection_metadata_map_.end()) {
    return metadata_itr->second;
  }

  // The collection should be allocated (i.e. all constraints set).
  auto& collection = collection_itr->second;
  if (!collection.BuffersAreAllocated()) {
    return std::nullopt;
  }

  // If the collection is in the map, and it's allocated, then we can return meta-data regarding
  // vmos and image constraints to the client.
  const auto& sysmem_info = collection.GetSysmemInfo();
  BufferCollectionMetadata result;
  result.vmo_count = sysmem_info.buffer_count;
  result.image_constraints = sysmem_info.settings.image_format_constraints;
  collection_metadata_map_[collection_id] = result;
  return result;
}

void VkRenderer::Render(const std::vector<ImageMetadata>& images) {
  auto vk_device = escher_->vk_device();
  auto vk_loader = escher_->device()->dispatch_loader();
  auto resource_recycler = escher_->resource_recycler();
  FX_DCHECK(vk_device);
  FX_DCHECK(resource_recycler);

  // TODO(51119): The vector |textures| is unused right now but will be used
  // once we implement actual rendering functionality.
  std::vector<escher::TexturePtr> textures;
  for (const auto& image : images) {
    GpuImageInfo gpu_info;
    {
      // TODO(44335): Convert this to a lock-free structure.
      std::unique_lock<std::mutex> lock(lock_);
      auto collection_itr = collection_map_.find(image.collection_id);
      FX_DCHECK(collection_itr != collection_map_.end());
      auto& collection = collection_itr->second;

      auto vk_itr = vk_collection_map_.find(image.collection_id);
      FX_DCHECK(vk_itr != vk_collection_map_.end());
      auto vk_collection = vk_itr->second;

      // Create the GPU info from the server side collection.
      auto gpu_info = GpuImageInfo::New(vk_device, vk_loader, collection.GetSysmemInfo(),
                                      vk_collection, image.vmo_idx);
      FX_DCHECK(gpu_info.GetGpuMem());
    }

    // Create an image from the server side collection.
    auto image_ptr = escher::image_utils::NewImage(
        vk_device, gpu_info.NewVkImageCreateInfo(image.width, image.height), gpu_info.GetGpuMem(),
        resource_recycler);
    FX_DCHECK(image_ptr);

    // Create a texture from the image.
    auto texture =
        escher::Texture::New(resource_recycler, std::move(image_ptr), vk::Filter::eNearest);
    FX_DCHECK(texture);

    textures.push_back(std::move(texture));
  }
}

}  // namespace flatland
