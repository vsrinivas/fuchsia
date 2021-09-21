// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_SCREENSHOT_SCREENSHOT_BUFFER_COLLECTION_IMPORTER_H_
#define SRC_UI_SCENIC_LIB_SCREENSHOT_SCREENSHOT_BUFFER_COLLECTION_IMPORTER_H_

#include <fuchsia/sysmem/cpp/fidl.h>

#include "src/ui/lib/escher/escher.h"
#include "src/ui/scenic/lib/allocation/buffer_collection_importer.h"

#include <vulkan/vulkan.hpp>

namespace screenshot {

class ScreenshotBufferCollectionImporter : public allocation::BufferCollectionImporter {
 public:
  explicit ScreenshotBufferCollectionImporter(escher::EscherWeakPtr escher);
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
  escher::ImagePtr ExtractImage(const allocation::ImageMetadata& metadata,
                                vk::BufferCollectionFUCHSIAX collection, vk::ImageUsageFlags usage);

  // Dispatcher where this class runs on. Currently points to scenic main thread's dispatcher.
  async_dispatcher_t* dispatcher_;

  // Escher gives us access to Vulkan.
  escher::EscherWeakPtr escher_;

  struct BufferCollectionInfo {
    vk::BufferCollectionFUCHSIAX vk_buffer_collection;
    fuchsia::sysmem::BufferCollectionSyncPtr buffer_collection_sync_ptr;
  };

  // Store all registered buffer collections and their related information.
  std::unordered_map<allocation::GlobalBufferCollectionId, BufferCollectionInfo>
      buffer_collection_infos_;
};

}  // namespace screenshot

#endif  // SRC_UI_SCENIC_LIB_SCREENSHOT_SCREENSHOT_BUFFER_COLLECTION_IMPORTER_H_
