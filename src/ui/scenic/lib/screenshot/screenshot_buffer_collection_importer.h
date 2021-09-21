// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_SCREENSHOT_SCREENSHOT_BUFFER_COLLECTION_IMPORTER_H_
#define SRC_UI_SCENIC_LIB_SCREENSHOT_SCREENSHOT_BUFFER_COLLECTION_IMPORTER_H_

#include <fuchsia/sysmem/cpp/fidl.h>

#include <unordered_set>

#include "src/ui/lib/escher/escher.h"
#include "src/ui/scenic/lib/allocation/buffer_collection_importer.h"
#include "src/ui/scenic/lib/flatland/renderer/vk_renderer.h"

#include <vulkan/vulkan.hpp>

namespace screenshot {

class ScreenshotBufferCollectionImporter : public allocation::BufferCollectionImporter {
 public:
  explicit ScreenshotBufferCollectionImporter(std::shared_ptr<flatland::VkRenderer> renderer);
  ~ScreenshotBufferCollectionImporter() override;

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

}  // namespace screenshot

#endif  // SRC_UI_SCENIC_LIB_SCREENSHOT_SCREENSHOT_BUFFER_COLLECTION_IMPORTER_H_
