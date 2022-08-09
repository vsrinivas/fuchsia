// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "screen_capture_buffer_collection_importer.h"

#include <lib/async/default.h>
#include <lib/fit/defer.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>
#include <zircon/status.h>

#include <cstdint>
#include <optional>

#include "src/ui/scenic/lib/allocation/buffer_collection_importer.h"

namespace {

using allocation::BufferCollectionUsage;
// Image formats supported by Scenic in a priority order.
const vk::Format kSupportedImageFormats[] = {vk::Format::eR8G8B8A8Srgb, vk::Format::eB8G8R8A8Srgb};
}  // anonymous namespace

namespace screen_capture {

ScreenCaptureBufferCollectionImporter::ScreenCaptureBufferCollectionImporter(
    fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator,
    std::shared_ptr<flatland::Renderer> renderer, bool enable_copy_fallback)
    : sysmem_allocator_(std::move(sysmem_allocator)),
      renderer_(std::move(renderer)),
      enable_copy_fallback_(enable_copy_fallback) {}

ScreenCaptureBufferCollectionImporter::~ScreenCaptureBufferCollectionImporter() {
  for (auto id : buffer_collections_) {
    renderer_->ReleaseBufferCollection(id, BufferCollectionUsage::kRenderTarget);
  }
  buffer_collections_.clear();
}

bool ScreenCaptureBufferCollectionImporter::ImportBufferCollection(
    allocation::GlobalBufferCollectionId collection_id,
    fuchsia::sysmem::Allocator_Sync* sysmem_allocator,
    fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token,
    BufferCollectionUsage usage, std::optional<fuchsia::math::SizeU> size) {
  TRACE_DURATION("gfx", "ScreenCaptureBufferCollectionImporter::ImportBufferCollection");
  // Expect only RenderTarget usage.
  FX_DCHECK(usage == BufferCollectionUsage::kRenderTarget);

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
  fuchsia::sysmem::BufferCollectionSyncPtr local_buffer_collection;

  fuchsia::sysmem::BufferCollectionTokenSyncPtr local_token = token.BindSync();
  fuchsia::sysmem::BufferCollectionTokenSyncPtr vulkan_token;
  zx_status_t status =
      local_token->Duplicate(std::numeric_limits<uint32_t>::max(), vulkan_token.NewRequest());
  if (status != ZX_OK) {
    FX_LOGS(WARNING) << __func__ << "failed, could not duplicate token: " << status;
    return false;
  }

  sysmem_allocator->BindSharedCollection(std::move(local_token),
                                         local_buffer_collection.NewRequest());
  status = local_buffer_collection->Sync();

  if (status != ZX_OK) {
    FX_LOGS(WARNING) << __func__ << " failed, could not bind buffer collection: " << status;
    return false;
  }

  status = local_buffer_collection->SetConstraints(false,
                                                   fuchsia::sysmem::BufferCollectionConstraints());
  if (status != ZX_OK) {
    FX_LOGS(WARNING) << __func__ << " failed, could not set constraints: " << status;
    return false;
  }

  if (!enable_copy_fallback_) {
    const bool success =
        renderer_->ImportBufferCollection(collection_id, sysmem_allocator, std::move(vulkan_token),
                                          BufferCollectionUsage::kRenderTarget, size);
    if (!success) {
      ReleaseBufferCollection(collection_id, BufferCollectionUsage::kRenderTarget);
      FX_LOGS(WARNING) << __func__ << " failed, could not register with Renderer";
      return false;
    }
  } else {
    fuchsia::sysmem::BufferCollectionTokenSyncPtr readback_sync_token = std::move(vulkan_token);
    fuchsia::sysmem::BufferCollectionTokenHandle readback_token;
    fuchsia::sysmem::BufferCollectionTokenHandle render_target_token;
    zx_status_t status;

    status = readback_sync_token->Duplicate(std::numeric_limits<uint32_t>::max(),
                                            readback_token.NewRequest());
    if (status != ZX_OK) {
      FX_LOGS(WARNING) << "Cannot duplicate readback sync token: " << zx_status_get_string(status)
                       << "; The client may have invalidated the token.";
      return false;
    }

    fuchsia::sysmem::BufferCollectionSyncPtr readback_collection;
    status = sysmem_allocator->BindSharedCollection(std::move(readback_sync_token),
                                                    readback_collection.NewRequest());
    if (status != ZX_OK) {
      FX_LOGS(WARNING) << "Cannot bind readback sync token: " << zx_status_get_string(status)
                       << "; The client may have invalidated the token.";
      return false;
    }

    status = readback_collection->Sync();
    if (status != ZX_OK) {
      FX_LOGS(WARNING) << "Could not sync readback buffer collection: "
                       << zx_status_get_string(status);
      return false;
    }

    if (!renderer_->ImportBufferCollection(collection_id, sysmem_allocator,
                                           std::move(readback_token),
                                           BufferCollectionUsage::kReadback, std::nullopt)) {
      FX_LOGS(WARNING) << "Could not register readback token with VkRenderer";
      return false;
    }

    status = readback_collection->SetConstraints(false /* has_constraints */,
                                                 fuchsia::sysmem::BufferCollectionConstraints());
    if (status != ZX_OK) {
      renderer_->ReleaseBufferCollection(collection_id, BufferCollectionUsage::kReadback);
      FX_LOGS(WARNING) << "Cannot set constraints on readback collection: "
                       << zx_status_get_string(status);
      return false;
    }

    // TODO(fxbug.dev/74423): Replace with prunable token when it is available.
    status =
        readback_collection->AttachToken(ZX_RIGHT_SAME_RIGHTS, render_target_token.NewRequest());
    if (status != ZX_OK) {
      renderer_->ReleaseBufferCollection(collection_id, BufferCollectionUsage::kReadback);
      FX_LOGS(WARNING) << "Cannot create render target sync token via AttachToken: "
                       << zx_status_get_string(status);
      return false;
    }

    status = readback_collection->Sync();
    if (status != ZX_OK) {
      FX_LOGS(WARNING) << "Could not sync readback buffer collection: "
                       << zx_status_get_string(status);
      return false;
    }

    if (!renderer_->ImportBufferCollection(collection_id, sysmem_allocator,
                                           std::move(render_target_token),
                                           BufferCollectionUsage::kRenderTarget, std::nullopt)) {
      renderer_->ReleaseBufferCollection(collection_id, BufferCollectionUsage::kReadback);
      FX_LOGS(WARNING) << "Could not register render target token with VkRenderer";
      return false;
    }

    status = readback_collection->Close();
    if (status != ZX_OK) {
      renderer_->ReleaseBufferCollection(collection_id, BufferCollectionUsage::kRenderTarget);
      renderer_->ReleaseBufferCollection(collection_id, BufferCollectionUsage::kReadback);
      FX_LOGS(WARNING) << "Cannot close readback collection: " << zx_status_get_string(status);
      return false;
    }
  }

  buffer_collection_sync_ptrs_[collection_id] = std::move(local_buffer_collection);
  buffer_collections_.insert(collection_id);

  return true;
}

void ScreenCaptureBufferCollectionImporter::ReleaseBufferCollection(
    allocation::GlobalBufferCollectionId collection_id, BufferCollectionUsage usage) {
  TRACE_DURATION("gfx", "ScreenCaptureBufferCollectionImporter::ReleaseBufferCollection");

  // If the collection is not in the map, then there's nothing to do.
  if (buffer_collections_.find(collection_id) == buffer_collections_.end()) {
    FX_LOGS(WARNING) << "Attempting to release a non-existent buffer collection.";
    return;
  }

  buffer_collections_.erase(collection_id);
  reset_render_targets_.erase(collection_id);

  if (buffer_collection_sync_ptrs_.find(collection_id) != buffer_collection_sync_ptrs_.end()) {
    fuchsia::sysmem::BufferCollectionSyncPtr buffer_collection;
    buffer_collection_sync_ptrs_.erase(collection_id);
  };

  if (buffer_collection_buffer_counts_.find(collection_id) !=
      buffer_collection_buffer_counts_.end()) {
    buffer_collection_buffer_counts_.erase(collection_id);
  };

  renderer_->ReleaseBufferCollection(collection_id, usage);
}

bool ScreenCaptureBufferCollectionImporter::ImportBufferImage(
    const allocation::ImageMetadata& metadata, BufferCollectionUsage usage) {
  TRACE_DURATION("gfx", "ScreenCaptureBufferCollectionImporter::ImportBufferImage");

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

  if (buffer_count == std::nullopt) {
    FX_LOGS(WARNING) << __func__ << " failed, buffer_count invalid";
    return false;
  }

  if (metadata.vmo_index >= buffer_count.value()) {
    FX_LOGS(WARNING) << __func__ << " failed, vmo_index " << metadata.vmo_index << " is invalid";
    return false;
  }

  if (renderer_->ImportBufferImage(metadata, BufferCollectionUsage::kRenderTarget)) {
    if (reset_render_targets_.find(metadata.collection_id) != reset_render_targets_.end()) {
      renderer_->ImportBufferImage(metadata, BufferCollectionUsage::kReadback);
    } else if (enable_copy_fallback_) {
      renderer_->ReleaseBufferCollection(metadata.collection_id, BufferCollectionUsage::kReadback);
    }
  } else if (enable_copy_fallback_) {
    // Try to re-allocate and re-import render targets.
    if (!ResetRenderTargetsForReadback(metadata, buffer_count.value())) {
      FX_LOGS(WARNING) << "Cannot reallocate readback render targets!";
      return false;
    }
    if (!renderer_->ImportBufferImage(metadata, BufferCollectionUsage::kReadback)) {
      FX_LOGS(WARNING) << "Could not import fallback render target to VkRenderer";
      return false;
    }
    if (!renderer_->ImportBufferImage(metadata, BufferCollectionUsage::kRenderTarget)) {
      FX_LOGS(WARNING) << "Could not import fallback render target to VkRenderer";
      return false;
    }
  } else {
    FX_LOGS(WARNING) << "Could not import render target to VkRenderer";
    return false;
  }
  return true;
}

void ScreenCaptureBufferCollectionImporter::ReleaseBufferImage(allocation::GlobalImageId image_id) {
  TRACE_DURATION("gfx", "ScreenCaptureBufferCollectionImporter::ReleaseBufferImage");
  renderer_->ReleaseBufferImage(image_id);
}

std::optional<BufferCount> ScreenCaptureBufferCollectionImporter::GetBufferCollectionBufferCount(
    allocation::GlobalBufferCollectionId collection_id) {
  // If the collection info has not been retrieved before, wait for the buffers to be allocated
  // and populate the map/delete the reference to the |collection_id| from
  // |collection_id_sync_ptrs_|.
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

bool ScreenCaptureBufferCollectionImporter::ResetRenderTargetsForReadback(
    const allocation::ImageMetadata& metadata, uint32_t buffer_count) {
  FX_DCHECK(enable_copy_fallback_);
  // Resetting render target for readback only should happen once at the first ImportBufferImage
  // from that BufferCollection. Don't do it again if this method had already been called for this
  // |metadata.collection_id|.
  if (reset_render_targets_.find(metadata.collection_id) != reset_render_targets_.end()) {
    return true;
  }

  FX_LOGS(WARNING) << "Could not import render target to VkRenderer; attempting to create fallback";
  renderer_->ReleaseBufferCollection(metadata.collection_id, BufferCollectionUsage::kRenderTarget);

  auto deregister_collection =
      fit::defer([renderer = renderer_, collection_id = metadata.collection_id] {
        renderer->ReleaseBufferCollection(collection_id, BufferCollectionUsage::kReadback);
      });

  fuchsia::sysmem::BufferCollectionTokenSyncPtr fallback_render_target_sync_token;
  zx_status_t status =
      sysmem_allocator_->AllocateSharedCollection(fallback_render_target_sync_token.NewRequest());
  if (status != ZX_OK) {
    FX_LOGS(WARNING) << "Cannot allocate fallback render target sync token: "
                     << zx_status_get_string(status);
    return false;
  }

  fuchsia::sysmem::BufferCollectionTokenHandle fallback_render_target_token;
  status = fallback_render_target_sync_token->Duplicate(std::numeric_limits<uint32_t>::max(),
                                                        fallback_render_target_token.NewRequest());
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Cannot duplicate fallback render target sync token: "
                   << zx_status_get_string(status);
    return false;
  }

  fuchsia::sysmem::BufferCollectionSyncPtr buffer_collection;
  status = sysmem_allocator_->BindSharedCollection(std::move(fallback_render_target_sync_token),
                                                   buffer_collection.NewRequest());
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Cannot bind fallback render target sync token: "
                   << zx_status_get_string(status);
    return false;
  }

  if (!renderer_->ImportBufferCollection(
          metadata.collection_id, sysmem_allocator_.get(), std::move(fallback_render_target_token),
          BufferCollectionUsage::kRenderTarget,
          std::optional<fuchsia::math::SizeU>({metadata.width, metadata.height}))) {
    FX_LOGS(WARNING) << "Could not register fallback render target with VkRenderer";
    return false;
  }

  fuchsia::sysmem::BufferCollectionConstraints constraints;
  constraints.min_buffer_count = buffer_count;
  constraints.usage.vulkan = fuchsia::sysmem::noneUsage;
  status = buffer_collection->SetConstraints(true, constraints);
  if (status != ZX_OK) {
    FX_LOGS(WARNING) << "Cannot set constraints on fallback render target collection: "
                     << zx_status_get_string(status);
    return false;
  }

  zx_status_t allocation_status = ZX_OK;
  fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection_info = {};
  status = buffer_collection->WaitForBuffersAllocated(&allocation_status, &buffer_collection_info);
  if (status != ZX_OK || allocation_status != ZX_OK) {
    FX_LOGS(WARNING) << "Could not wait on allocation for fallback render target collection: "
                     << zx_status_get_string(status)
                     << " ;alloc: " << zx_status_get_string(allocation_status);
    return false;
  }

  status = buffer_collection->Close();
  if (status != ZX_OK) {
    FX_LOGS(WARNING) << "Could not close fallback render target collection: "
                     << zx_status_get_string(status);
    return false;
  }

  reset_render_targets_.insert(metadata.collection_id);
  deregister_collection.cancel();
  return true;
}

}  // namespace screen_capture
