// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/renderer/null_renderer.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/pixelformat.h>

namespace flatland {

bool NullRenderer::RegisterRenderTargetCollection(
    sysmem_util::GlobalBufferCollectionId collection_id,
    fuchsia::sysmem::Allocator_Sync* sysmem_allocator,
    fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token) {
  return ImportBufferCollection(collection_id, sysmem_allocator, std::move(token));
}

void NullRenderer::DeregisterRenderTargetCollection(
    sysmem_util::GlobalBufferCollectionId collection_id) {
  ReleaseBufferCollection(collection_id);
}

bool NullRenderer::ImportBufferCollection(
    sysmem_util::GlobalBufferCollectionId collection_id,
    fuchsia::sysmem::Allocator_Sync* sysmem_allocator,
    fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token) {
  FX_DCHECK(collection_id != sysmem_util::kInvalidId);

  // Check for a null token here before we try to duplicate it to get the
  // vulkan token.
  if (!token.is_valid()) {
    FX_LOGS(ERROR) << "Token is invalid.";
    return false;
  }

  if (collection_map_.find(collection_id) != collection_map_.end()) {
    FX_LOGS(ERROR) << "Duplicate GlobalBufferCollectionID: " << collection_id;
    return false;
  }

  auto result = BufferCollectionInfo::New(sysmem_allocator, std::move(token));

  if (result.is_error()) {
    FX_LOGS(ERROR) << "Unable to register collection.";
    return false;
  }

  // Multiple threads may be attempting to read/write from |collection_map_| so we
  // lock this function here.
  // TODO(fxbug.dev/44335): Convert this to a lock-free structure.
  std::unique_lock<std::mutex> lock(lock_);
  collection_map_[collection_id] = std::move(result.value());
  return true;
}

void NullRenderer::ReleaseBufferCollection(sysmem_util::GlobalBufferCollectionId collection_id) {
  // Multiple threads may be attempting to read/write from the various maps,
  // lock this function here.
  // TODO(fxbug.dev/44335): Convert this to a lock-free structure.
  std::unique_lock<std::mutex> lock(lock_);

  auto collection_itr = collection_map_.find(collection_id);

  // If the collection is not in the map, then there's nothing to do.
  if (collection_itr == collection_map_.end()) {
    return;
  }

  // Erase the sysmem collection from the map.
  collection_map_.erase(collection_id);
}

bool NullRenderer::ImportImage(const ImageMetadata& meta_data) {
  std::unique_lock<std::mutex> lock(lock_);
  const auto& collection_itr = collection_map_.find(meta_data.collection_id);
  if (collection_itr == collection_map_.end()) {
    FX_LOGS(ERROR) << "Collection with id " << meta_data.collection_id << " does not exist.";
    return false;
  }

  auto& collection = collection_itr->second;
  if (!collection.BuffersAreAllocated()) {
    FX_LOGS(ERROR) << "Buffers for collection " << meta_data.collection_id
                   << " have not been allocated.";
    return false;
  }

  const auto& sysmem_info = collection.GetSysmemInfo();
  const auto vmo_count = sysmem_info.buffer_count;
  auto image_constraints = sysmem_info.settings.image_format_constraints;

  if (meta_data.vmo_idx >= vmo_count) {
    FX_LOGS(ERROR) << "CreateImage failed, vmo_index " << meta_data.vmo_idx
                   << " must be less than vmo_count " << vmo_count;
    return false;
  }

  if (meta_data.width < image_constraints.min_coded_width ||
      meta_data.width > image_constraints.max_coded_width) {
    FX_LOGS(ERROR) << "CreateImage failed, width " << meta_data.width
                   << " is not within valid range [" << image_constraints.min_coded_width << ","
                   << image_constraints.max_coded_width << "]";
    return false;
  }

  if (meta_data.height < image_constraints.min_coded_height ||
      meta_data.height > image_constraints.max_coded_height) {
    FX_LOGS(ERROR) << "CreateImage failed, height " << meta_data.height
                   << " is not within valid range [" << image_constraints.min_coded_height << ","
                   << image_constraints.max_coded_height << "]";
    return false;
  }

  return true;
}

void NullRenderer::ReleaseImage(GlobalImageId image_id) {}

// Check that the buffer collections for each of the images passed in have been validated.
// DCHECK if they have not.
void NullRenderer::Render(const ImageMetadata& render_target,
                          const std::vector<Rectangle2D>& rectangles,
                          const std::vector<ImageMetadata>& images,
                          const std::vector<zx::event>& release_fences) {
  for (const auto& image : images) {
    auto collection_id = image.collection_id;
    FX_DCHECK(collection_id != sysmem_util::kInvalidId);

    // TODO(fxbug.dev/44335): Convert this to a lock-free structure.
    std::unique_lock<std::mutex> lock(lock_);

    const auto& collection_map_itr = collection_map_.find(collection_id);
    FX_DCHECK(collection_map_itr != collection_map_.end());

    auto& collection = collection_map_itr->second;
    FX_DCHECK(collection.BuffersAreAllocated());

    const auto& sysmem_info = collection.GetSysmemInfo();

    const auto vmo_count = sysmem_info.buffer_count;
    auto image_constraints = sysmem_info.settings.image_format_constraints;

    // Make sure the image conforms to the constraints of the collection.
    FX_DCHECK(image.vmo_idx < vmo_count);
    FX_DCHECK(image.width <= image_constraints.max_coded_width);
    FX_DCHECK(image.height <= image_constraints.max_coded_height);
  }

  // Fire all of the release fences.
  for (auto& fence : release_fences) {
    fence.signal(0, ZX_EVENT_SIGNALED);
  }
}

}  // namespace flatland
