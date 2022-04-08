// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_SCREEN_CAPTURE_SCREEN_CAPTURE_BUFFER_COLLECTION_IMPORTER_H_
#define SRC_UI_SCENIC_LIB_SCREEN_CAPTURE_SCREEN_CAPTURE_BUFFER_COLLECTION_IMPORTER_H_

#include <fuchsia/sysmem/cpp/fidl.h>

#include <unordered_set>

#include "src/ui/scenic/lib/allocation/buffer_collection_importer.h"
#include "src/ui/scenic/lib/flatland/renderer/vk_renderer.h"

#include <vulkan/vulkan.hpp>

namespace screen_capture {

class ScreenCaptureBufferCollectionImporter : public allocation::BufferCollectionImporter {
 public:
  explicit ScreenCaptureBufferCollectionImporter(std::shared_ptr<flatland::VkRenderer> renderer);
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

 private:
  // Dispatcher where this class runs on. Currently points to scenic main thread's dispatcher.
  async_dispatcher_t* dispatcher_;

  std::shared_ptr<flatland::VkRenderer> renderer_;

  // Store all registered buffer collections.
  std::unordered_set<allocation::GlobalBufferCollectionId> buffer_collection_infos_;
};

}  // namespace screen_capture

#endif  // SRC_UI_SCENIC_LIB_SCREEN_CAPTURE_SCREEN_CAPTURE_BUFFER_COLLECTION_IMPORTER_H_
