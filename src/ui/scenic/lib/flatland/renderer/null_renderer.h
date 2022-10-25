// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_FLATLAND_RENDERER_NULL_RENDERER_H_
#define SRC_UI_SCENIC_LIB_FLATLAND_RENDERER_NULL_RENDERER_H_

#include <mutex>
#include <unordered_map>

#include "src/ui/scenic/lib/allocation/buffer_collection_importer.h"
#include "src/ui/scenic/lib/flatland/buffers/buffer_collection.h"
#include "src/ui/scenic/lib/flatland/flatland_types.h"
#include "src/ui/scenic/lib/flatland/renderer/renderer.h"

namespace flatland {

using allocation::BufferCollectionUsage;

// A renderer implementation used for validation. It does everything a standard
// renderer implementation does except for actually rendering.
class NullRenderer final : public Renderer {
 public:
  ~NullRenderer() override = default;

  // |BufferCollectionImporter|
  bool ImportBufferCollection(allocation::GlobalBufferCollectionId collection_id,
                              fuchsia::sysmem::Allocator_Sync* sysmem_allocator,
                              fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token,
                              BufferCollectionUsage usage,
                              std::optional<fuchsia::math::SizeU> size) override;

  // |BufferCollectionImporter|
  void ReleaseBufferCollection(allocation::GlobalBufferCollectionId collection_id,
                               BufferCollectionUsage usage) override;

  // |BufferCollectionImporter|
  bool ImportBufferImage(const allocation::ImageMetadata& metadata,
                         BufferCollectionUsage usage) override;

  // |BufferCollectionImporter|
  void ReleaseBufferImage(allocation::GlobalImageId image_id) override;

  // |Renderer|.
  void Render(const allocation::ImageMetadata& render_target,
              const std::vector<ImageRect>& rectangles,
              const std::vector<allocation::ImageMetadata>& images,
              const std::vector<zx::event>& release_fences = {},
              bool apply_color_conversion = false) override;

  // |Renderer|.
  void SetColorConversionValues(const std::array<float, 9>& coefficients,
                                const std::array<float, 3>& preoffsets,
                                const std::array<float, 3>& postoffsets) override;

  // |Renderer|.
  zx_pixel_format_t ChoosePreferredPixelFormat(
      const std::vector<zx_pixel_format_t>& available_formats) const override;

  // |Renderer|.
  bool SupportsRenderInProtected() const override;

  // |Renderer|.
  bool RequiresRenderInProtected(
      const std::vector<allocation::ImageMetadata>& images) const override;

 private:
  // This mutex is used to protect access to |collection_map_| and |image_map|.
  std::mutex lock_;
  std::unordered_map<allocation::GlobalBufferCollectionId, BufferCollectionInfo> render_target_map_;
  std::unordered_map<allocation::GlobalBufferCollectionId, BufferCollectionInfo> readback_map_;
  std::unordered_map<allocation::GlobalImageId, fuchsia::sysmem::ImageFormatConstraints> image_map_;
};

}  // namespace flatland

#endif  // SRC_UI_SCENIC_LIB_FLATLAND_RENDERER_NULL_RENDERER_H_
