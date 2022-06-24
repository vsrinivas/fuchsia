// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "screen_capture_buffer_collection_importer.h"

#include <lib/async/default.h>
#include <lib/trace/event.h>

#include <optional>

namespace {
// Image formats supported by Scenic in a priority order.
const vk::Format kSupportedImageFormats[] = {vk::Format::eR8G8B8A8Srgb, vk::Format::eB8G8R8A8Srgb};
}  // anonymous namespace

namespace screen_capture {

ScreenCaptureBufferCollectionImporter::ScreenCaptureBufferCollectionImporter(
    std::shared_ptr<flatland::Renderer> renderer)
    : dispatcher_(async_get_default_dispatcher()), renderer_(renderer) {}

ScreenCaptureBufferCollectionImporter::~ScreenCaptureBufferCollectionImporter() {
  for (auto& id : buffer_collections_) {
    renderer_->ReleaseBufferCollection(id);
  }
  buffer_collections_.clear();
}

bool ScreenCaptureBufferCollectionImporter::ImportBufferCollection(
    allocation::GlobalBufferCollectionId collection_id,
    fuchsia::sysmem::Allocator_Sync* sysmem_allocator,
    fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token) {
  TRACE_DURATION("gfx", "ScreenCaptureBufferCollectionImporter::ImportBufferCollection");

  if (!token.is_valid()) {
    FX_LOGS(WARNING) << "ImportBufferCollection called with invalid token";
    return false;
  }

  if (buffer_collections_.find(collection_id) != buffer_collections_.end()) {
    FX_LOGS(WARNING) << __func__ << " failed, called with pre-existing collection_id "
                     << collection_id << ".";
    return false;
  }

  // Tie the |buffer_collection_info| to the |collection_id| using the |local_token|.
  fuchsia::sysmem::BufferCollectionSyncPtr buffer_collection;

  fuchsia::sysmem::BufferCollectionTokenSyncPtr local_token = token.BindSync();
  fuchsia::sysmem::BufferCollectionTokenSyncPtr vulkan_token;
  zx_status_t status =
      local_token->Duplicate(std::numeric_limits<uint32_t>::max(), vulkan_token.NewRequest());
  if (status != ZX_OK) {
    FX_LOGS(WARNING) << __func__ << "failed, could not duplicate token: " << status;
    return false;
  }

  sysmem_allocator->BindSharedCollection(std::move(local_token), buffer_collection.NewRequest());
  status = buffer_collection->Sync();

  if (status != ZX_OK) {
    FX_LOGS(WARNING) << __func__ << " failed, could not bind buffer collection: " << status;
    return false;
  }

  status = buffer_collection->SetConstraints(false, fuchsia::sysmem::BufferCollectionConstraints());
  if (status != ZX_OK) {
    FX_LOGS(WARNING) << __func__ << " failed, could not set constraints: " << status;
    return false;
  }

  buffer_collection_sync_ptrs_[collection_id] = std::move(buffer_collection);

  const bool success = renderer_->RegisterRenderTargetCollection(collection_id, sysmem_allocator,
                                                                 std::move(vulkan_token));
  if (!success) {
    ReleaseBufferCollection(collection_id);
    FX_LOGS(WARNING) << __func__ << " failed, could not register with Renderer";
    return false;
  }

  buffer_collections_.insert(collection_id);

  return true;
}

void ScreenCaptureBufferCollectionImporter::ReleaseBufferCollection(
    allocation::GlobalBufferCollectionId collection_id) {
  TRACE_DURATION("gfx", "ScreenCaptureBufferCollectionImporter::ReleaseBufferCollection");

  auto collection_itr = buffer_collections_.find(collection_id);

  // If the collection is not in the map, then there's nothing to do.
  if (collection_itr == buffer_collections_.end()) {
    FX_LOGS(WARNING) << "Attempting to release a non-existent buffer collection.";
    return;
  }

  buffer_collections_.erase(collection_id);

  if (buffer_collection_sync_ptrs_.find(collection_id) != buffer_collection_sync_ptrs_.end()) {
    fuchsia::sysmem::BufferCollectionSyncPtr buffer_collection;
    buffer_collection_sync_ptrs_.erase(collection_id);
  };

  if (buffer_collection_buffer_counts_.find(collection_id) !=
      buffer_collection_buffer_counts_.end()) {
    buffer_collection_buffer_counts_.erase(collection_id);
  };

  renderer_->DeregisterRenderTargetCollection(collection_id);
}

bool ScreenCaptureBufferCollectionImporter::ImportBufferImage(
    const allocation::ImageMetadata& metadata) {
  // The metadata can't have an invalid |collection_id|.
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
  auto collection_itr = buffer_collections_.find(metadata.collection_id);
  if (collection_itr == buffer_collections_.end()) {
    FX_LOGS(WARNING) << "Collection with id " << metadata.collection_id << " does not exist.";
    return false;
  }

  const std::optional<uint32_t> buffer_count =
      GetBufferCollectionBufferCount(metadata.collection_id);

  if (!buffer_count.value()) {
    FX_LOGS(WARNING) << __func__ << " failed, buffer_count invalid";
    return false;
  }

  if (metadata.vmo_index >= buffer_count) {
    FX_LOGS(WARNING) << __func__ << " failed, vmo_index " << metadata.vmo_index << " is invalid";
    return false;
  }

  const bool success = renderer_->ImportBufferImage(metadata);
  if (!success) {
    FX_LOGS(WARNING) << __func__ << " failed, could not import to Renderer";
    return false;
  }

  return true;
}

void ScreenCaptureBufferCollectionImporter::ReleaseBufferImage(allocation::GlobalImageId image_id) {
  renderer_->ReleaseBufferImage(image_id);
}

std::optional<uint32_t> ScreenCaptureBufferCollectionImporter::GetBufferCollectionBufferCount(
    allocation::GlobalBufferCollectionId collection_id) {
  // If the collection info has not been retrieved before, wait for the buffers to be allocated and
  // populate the map/delete the reference to the |collection_id| from |collection_id_sync_ptrs_|.
  if (buffer_collection_buffer_counts_.find(collection_id) ==
      buffer_collection_buffer_counts_.end()) {
    fuchsia::sysmem::BufferCollectionSyncPtr buffer_collection;
    zx_status_t allocation_status = ZX_OK;

    if (buffer_collection_sync_ptrs_.find(collection_id) == buffer_collection_sync_ptrs_.end()) {
      FX_LOGS(WARNING) << "Collection with id " << collection_id << " does not exist.";
      return std::nullopt;
    }

    buffer_collection = std::move(buffer_collection_sync_ptrs_[collection_id]);

    buffer_collection->CheckBuffersAllocated(&allocation_status);
    if (allocation_status != ZX_OK) {
      FX_LOGS(WARNING) << __func__ << " failed, no buffers allocated: " << allocation_status;
      return std::nullopt;
    }

    fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection_info = {};
    buffer_collection->WaitForBuffersAllocated(&allocation_status, &buffer_collection_info);
    if (allocation_status != ZX_OK) {
      FX_LOGS(WARNING) << __func__
                       << " failed, waiting on no buffers allocated: " << allocation_status;
      return std::nullopt;
    }

    buffer_collection_sync_ptrs_.erase(collection_id);
    buffer_collection->Close();

    buffer_collection_buffer_counts_[collection_id] = buffer_collection_info.buffer_count;
  }

  return buffer_collection_buffer_counts_[collection_id];
}

}  // namespace screen_capture
