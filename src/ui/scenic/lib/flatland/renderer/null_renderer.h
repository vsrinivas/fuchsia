// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_FLATLAND_RENDERER_NULL_RENDERER_H_
#define SRC_UI_SCENIC_LIB_FLATLAND_RENDERER_NULL_RENDERER_H_

#include <mutex>
#include <unordered_map>

#include "src/ui/scenic/lib/flatland/buffers/buffer_collection.h"
#include "src/ui/scenic/lib/flatland/renderer/renderer.h"

namespace flatland {

// A renderer implementation used for validation. It does everything a standard
// renderer implementation does except for actually rendering.
class NullRenderer final : public Renderer {
 public:
  ~NullRenderer() override = default;

  // |BufferCollectionImporter|
  bool ImportBufferCollection(
      sysmem_util::GlobalBufferCollectionId collection_id,
      fuchsia::sysmem::Allocator_Sync* sysmem_allocator,
      fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token) override;

  // |BufferCollectionImporter|
  void ReleaseBufferCollection(sysmem_util::GlobalBufferCollectionId collection_id) override;

  // |BufferCollectionImporter|
  bool ImportImage(const ImageMetadata& meta_data) override;

  // |BufferCollectionImporter|
  void ReleaseImage(GlobalImageId image_id) override;

  // |Renderer|.
  bool RegisterRenderTargetCollection(
      sysmem_util::GlobalBufferCollectionId collection_id,
      fuchsia::sysmem::Allocator_Sync* sysmem_allocator,
      fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token) override;

  // |Renderer|.
  void DeregisterRenderTargetCollection(
      sysmem_util::GlobalBufferCollectionId collection_id) override;

  // |Renderer|.
  std::optional<BufferCollectionMetadata> Validate(
      sysmem_util::GlobalBufferCollectionId collection_id) override;

  // |Renderer|.
  void Render(const ImageMetadata& render_target, const std::vector<Rectangle2D>& rectangles,
              const std::vector<ImageMetadata>& images,
              const std::vector<zx::event>& release_fences = {}) override;

 private:
  // This mutex is used to protect access to |collection_map_| and |collection_metadata_map_|.
  std::mutex lock_;
  std::unordered_map<sysmem_util::GlobalBufferCollectionId, BufferCollectionInfo> collection_map_;
  std::unordered_map<sysmem_util::GlobalBufferCollectionId, BufferCollectionMetadata>
      collection_metadata_map_;
};

}  // namespace flatland

#endif  // SRC_UI_SCENIC_LIB_FLATLAND_RENDERER_NULL_RENDERER_H_
