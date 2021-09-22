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

ScreenshotBufferCollectionImporter::ScreenshotBufferCollectionImporter(
    std::shared_ptr<flatland::VkRenderer> renderer)
    : dispatcher_(async_get_default_dispatcher()), renderer_(renderer) {}

ScreenshotBufferCollectionImporter::~ScreenshotBufferCollectionImporter() {
  for (auto& id : buffer_collection_infos_) {
    renderer_->ReleaseBufferCollection(id);
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

  bool success = renderer_->RegisterRenderTargetCollection(collection_id, sysmem_allocator,
                                                           std::move(token));
  if (!success) {
    ReleaseBufferCollection(collection_id);
    FX_LOGS(WARNING) << __func__ << " failed, could not register with VkRenderer";
    return false;
  }

  buffer_collection_infos_.insert(collection_id);

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

  buffer_collection_infos_.erase(collection_id);

  renderer_->DeregisterRenderTargetCollection(collection_id);
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

  bool success = renderer_->ImportBufferImage(metadata);
  if (!success) {
    FX_LOGS(WARNING) << __func__ << " failed, could not import to VkRenderer";
    return false;
  }

  return true;
}

void ScreenshotBufferCollectionImporter::ReleaseBufferImage(allocation::GlobalImageId image_id) {
  renderer_->ReleaseBufferImage(image_id);
}

}  // namespace screenshot
