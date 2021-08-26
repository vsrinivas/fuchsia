// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_ENGINE_GFX_BUFFER_COLLECTION_IMPORTER_H_
#define SRC_UI_SCENIC_LIB_GFX_ENGINE_GFX_BUFFER_COLLECTION_IMPORTER_H_

#include <fuchsia/sysmem/cpp/fidl.h>

#include "src/ui/lib/escher/escher.h"
#include "src/ui/scenic/lib/allocation/buffer_collection_importer.h"
#include "src/ui/scenic/lib/gfx/id.h"

#include <vulkan/vulkan.hpp>

namespace scenic_impl {
namespace gfx {

class GpuImage;
class Session;

class GfxBufferCollectionImporter : public allocation::BufferCollectionImporter {
 public:
  explicit GfxBufferCollectionImporter(escher::EscherWeakPtr escher);
  ~GfxBufferCollectionImporter() override;

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

  // Moves the ownership of an Image imported into this class to the caller's |session|. Note that
  // ImportBufferImage() must be called before with |image_id|.
  fxl::RefPtr<GpuImage> ExtractImage(Session* session, const allocation::ImageMetadata& metadata,
                                     ResourceId id);

 private:
  // Dispatcher where this class runs on. Currently points to scenic main thread's dispatcher.
  async_dispatcher_t* dispatcher_;

  // Escher gives us access to Vulkan.
  escher::EscherWeakPtr escher_;

  // Helper struct to hold information about the imported BufferCollections.
  struct BufferCollectionInfo {
    vk::BufferCollectionFUCHSIAX vk_buffer_collection;
    fuchsia::sysmem::BufferCollectionSyncPtr buffer_collection_sync_ptr;
  };
  std::unordered_map<allocation::GlobalBufferCollectionId, BufferCollectionInfo>
      buffer_collection_infos_;
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_GFX_ENGINE_GFX_BUFFER_COLLECTION_IMPORTER_H_
