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
      allocation::GlobalBufferCollectionId collection_id,
      fuchsia::sysmem::Allocator_Sync* sysmem_allocator,
      fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token) override;

  // |BufferCollectionImporter|
  void ReleaseBufferCollection(allocation::GlobalBufferCollectionId collection_id) override;

  // |BufferCollectionImporter|
  bool ImportBufferImage(const allocation::ImageMetadata& metadata) override;

  // |BufferCollectionImporter|
  void ReleaseBufferImage(allocation::GlobalImageId image_id) override;

  // |Renderer|.
  bool RegisterRenderTargetCollection(
      allocation::GlobalBufferCollectionId collection_id,
      fuchsia::sysmem::Allocator_Sync* sysmem_allocator,
      fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token,
      fuchsia::math::SizeU size = {}) override;

  // |Renderer|.
  void DeregisterRenderTargetCollection(
      allocation::GlobalBufferCollectionId collection_id) override;

  // |Renderer|.
  void Render(const allocation::ImageMetadata& render_target,
              const std::vector<Rectangle2D>& rectangles,
              const std::vector<allocation::ImageMetadata>& images,
              const std::vector<zx::event>& release_fences = {}) override;

  // |Renderer|.
  zx_pixel_format_t ChoosePreferredPixelFormat(
      const std::vector<zx_pixel_format_t>& available_formats) const override;

 private:
  // This mutex is used to protect access to |collection_map_| and |image_map|.
  std::mutex lock_;
  std::unordered_map<allocation::GlobalBufferCollectionId, BufferCollectionInfo> collection_map_;
  std::unordered_map<allocation::GlobalImageId, fuchsia::sysmem::ImageFormatConstraints> image_map_;
};

}  // namespace flatland

#endif  // SRC_UI_SCENIC_LIB_FLATLAND_RENDERER_NULL_RENDERER_H_
