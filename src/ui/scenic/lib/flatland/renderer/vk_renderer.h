// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_FLATLAND_RENDERER_VK_RENDERER_H_
#define SRC_UI_SCENIC_LIB_FLATLAND_RENDERER_VK_RENDERER_H_

#include <fuchsia/images/cpp/fidl.h>

#include <set>
#include <unordered_map>

#include "src/ui/lib/escher/flatland/rectangle_compositor.h"
#include "src/ui/scenic/lib/flatland/renderer/renderer.h"

namespace flatland {

// Implementation of the Flatland Renderer interface that relies on Escher and
// by extension the Vulkan API.
class VkRenderer final : public Renderer {
 public:
  explicit VkRenderer(escher::EscherWeakPtr escher);
  ~VkRenderer() override;

  // |BufferCollectionImporter|
  bool ImportBufferCollection(
      sysmem_util::GlobalBufferCollectionId collection_id,
      fuchsia::sysmem::Allocator_Sync* sysmem_allocator,
      fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token) override;

  // |BufferCollectionImporter|
  void ReleaseBufferCollection(sysmem_util::GlobalBufferCollectionId collection_id) override;

  // |BufferCollectionImporter|
  bool ImportBufferImage(const ImageMetadata& metadata) override;

  // |BufferCollectionImporter|
  void ReleaseBufferImage(sysmem_util::GlobalImageId image_id) override;

  // |Renderer|.
  bool RegisterRenderTargetCollection(
      sysmem_util::GlobalBufferCollectionId collection_id,
      fuchsia::sysmem::Allocator_Sync* sysmem_allocator,
      fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token) override;

  // |Renderer|.
  void DeregisterRenderTargetCollection(
      sysmem_util::GlobalBufferCollectionId collection_id) override;

  // |Renderer|.
  void Render(const ImageMetadata& render_target, const std::vector<Rectangle2D>& rectangles,
              const std::vector<ImageMetadata>& images,
              const std::vector<zx::event>& release_fences = {}) override;

  // |Renderer|.
  zx_pixel_format_t ChoosePreferredPixelFormat(
      const std::vector<zx_pixel_format_t>& available_formats) const override;

  // Wait for all gpu operations to complete.
  void WaitIdle();

 private:
  // Generic helper function used by both |ImportBufferCollection| and |RegisterTextureCollection|.
  bool RegisterCollection(sysmem_util::GlobalBufferCollectionId collection_id,
                          fuchsia::sysmem::Allocator_Sync* sysmem_allocator,
                          fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token,
                          vk::ImageUsageFlags usage);

  // The function ExtractImage() creates an escher Image from a sysmem collection vmo.
  escher::ImagePtr ExtractImage(const ImageMetadata& metadata,
                                vk::BufferCollectionFUCHSIA collection, vk::ImageUsageFlags usage);

  // ExtractTexture() is a wrapper function to ExtractImage().
  escher::TexturePtr ExtractTexture(const ImageMetadata& metadata,
                                    vk::BufferCollectionFUCHSIA collection);

  // Escher is how we access Vulkan.
  escher::EscherWeakPtr escher_;

  // Vulkan rendering component.
  escher::RectangleCompositor compositor_;

  // This mutex is used to protect access to |collection_map_| and |sysmem_map_|.
  mutable std::mutex lock_;
  std::unordered_map<sysmem_util::GlobalBufferCollectionId, vk::BufferCollectionFUCHSIA>
      vk_collections_;
  std::unordered_map<sysmem_util::GlobalBufferCollectionId,
                     fuchsia::sysmem::BufferCollectionSyncPtr>
      collections_;

  std::unordered_map<sysmem_util::GlobalImageId, escher::TexturePtr> texture_map_;
  std::unordered_map<sysmem_util::GlobalImageId, escher::ImagePtr> render_target_map_;
  std::unordered_map<sysmem_util::GlobalImageId, escher::TexturePtr> depth_target_map_;
  std::set<sysmem_util::GlobalImageId> pending_textures_;
  std::set<sysmem_util::GlobalImageId> pending_render_targets_;

  uint32_t frame_number_ = 0;
};

}  // namespace flatland

#endif  // SRC_UI_SCENIC_LIB_FLATLAND_RENDERER_VK_RENDERER_H_
