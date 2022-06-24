// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_SCREEN_CAPTURE_SCREEN_CAPTURE_BUFFER_COLLECTION_IMPORTER_H_
#define SRC_UI_SCENIC_LIB_SCREEN_CAPTURE_SCREEN_CAPTURE_BUFFER_COLLECTION_IMPORTER_H_

#include <fuchsia/sysmem/cpp/fidl.h>

#include <unordered_map>
#include <unordered_set>

#include "src/ui/scenic/lib/allocation/buffer_collection_importer.h"
#include "src/ui/scenic/lib/allocation/id.h"
#include "src/ui/scenic/lib/flatland/renderer/renderer.h"

#include <vulkan/vulkan.hpp>

namespace screen_capture {

using BufferCount = uint32_t;

class ScreenCaptureBufferCollectionImporter : public allocation::BufferCollectionImporter {
 public:
  explicit ScreenCaptureBufferCollectionImporter(std::shared_ptr<flatland::Renderer> renderer);
  ~ScreenCaptureBufferCollectionImporter() override;

  // |BufferCollectionImporter|
  bool ImportBufferCollection(
      allocation::GlobalBufferCollectionId collection_id,
      fuchsia::sysmem::Allocator_Sync* sysmem_allocator,
      fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token) override;

  // |BufferCollectionImporter|
  void ReleaseBufferCollection(allocation::GlobalBufferCollectionId collection_id) override;

  // |BufferCollectionImporter|
  bool ImportBufferImage(const allocation::ImageMetadata& metadata) override;

  // |BufferCollectionImporter|
  void ReleaseBufferImage(allocation::GlobalImageId image_id) override;

  // A |BufferCount| will be returned if all buffers have been allocated and the
  // collection_id exists. Otherwise, |std::nullopt| will be returned.
  std::optional<BufferCount> GetBufferCollectionBufferCount(
      allocation::GlobalBufferCollectionId collection_id);

 private:
  // Dispatcher where this class runs on. Currently points to scenic main thread's dispatcher.
  async_dispatcher_t* dispatcher_;

  std::shared_ptr<flatland::Renderer> renderer_;

  // |buffer_collection_sync_ptrs_| is populated during the call to ImportBufferCollection().
  // |buffer_collection_buffer_counts_| is lazily populated after buffers are allocated, during a
  // call to GetBufferCollectionBufferCount() or ImportBufferImage(). If the
  // |GlobalBufferCollectionId| key exists in one map, it does not exist in the other.
  std::unordered_map<allocation::GlobalBufferCollectionId, fuchsia::sysmem::BufferCollectionSyncPtr>
      buffer_collection_sync_ptrs_;
  std::unordered_map<allocation::GlobalBufferCollectionId, BufferCount>
      buffer_collection_buffer_counts_;

  // Store all registered buffer collections.
  std::unordered_set<allocation::GlobalBufferCollectionId> buffer_collections_;
};

}  // namespace screen_capture

#endif  // SRC_UI_SCENIC_LIB_SCREEN_CAPTURE_SCREEN_CAPTURE_BUFFER_COLLECTION_IMPORTER_H_
