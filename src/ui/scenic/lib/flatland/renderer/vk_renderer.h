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

using allocation::GlobalBufferCollectionId;
using allocation::GlobalImageId;
using allocation::ImageMetadata;

// Implementation of the Flatland Renderer interface that relies on Escher and
// by extension the Vulkan API.
class VkRenderer final : public Renderer {
 public:
  explicit VkRenderer(escher::EscherWeakPtr escher);
  ~VkRenderer() override;

  // |BufferCollectionImporter|
  bool ImportBufferCollection(
      GlobalBufferCollectionId collection_id, fuchsia::sysmem::Allocator_Sync* sysmem_allocator,
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
      fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token) override;

  // |Renderer|.
  void DeregisterRenderTargetCollection(GlobalBufferCollectionId collection_id) override;

  // |Renderer|.
  void Render(const allocation::ImageMetadata& render_target,
              const std::vector<Rectangle2D>& rectangles,
              const std::vector<allocation::ImageMetadata>& images,
              const std::vector<zx::event>& release_fences = {}) override;

  // |Renderer|.
  zx_pixel_format_t ChoosePreferredPixelFormat(
      const std::vector<zx_pixel_format_t>& available_formats) const override;

  // Wait for all gpu operations to complete.
  void WaitIdle();

 private:
  // Wrapper struct to contain the sysmem collection handle, the vulkan
  // buffer collection and a bool to determine if the collection is meant
  // to be used for render targets or if it's meant to be used for
  // client textures.
  struct CollectionData {
    fuchsia::sysmem::BufferCollectionSyncPtr collection;
    vk::BufferCollectionFUCHSIAX vk_collection;
    bool is_render_target;
  };

  // Generic helper function used by both |ImportBufferCollection| and |RegisterTextureCollection|.
  bool RegisterCollection(allocation::GlobalBufferCollectionId collection_id,
                          fuchsia::sysmem::Allocator_Sync* sysmem_allocator,
                          fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token,
                          vk::ImageUsageFlags usage);

  // The function ExtractImage() creates an escher Image from a sysmem collection vmo.
  escher::ImagePtr ExtractImage(const allocation::ImageMetadata& metadata,
                                vk::BufferCollectionFUCHSIAX collection, vk::ImageUsageFlags usage);

  // ExtractTexture() is a wrapper function to ExtractImage().
  escher::TexturePtr ExtractTexture(const allocation::ImageMetadata& metadata,
                                    vk::BufferCollectionFUCHSIAX collection);

  // Escher is how we access Vulkan.
  escher::EscherWeakPtr escher_;

  // Vulkan rendering component.
  escher::RectangleCompositor compositor_;

  // This mutex is used to protect access to |collections_|.
  mutable std::mutex lock_;
  std::unordered_map<allocation::GlobalBufferCollectionId, CollectionData> collections_;

  std::unordered_map<GlobalImageId, escher::TexturePtr> texture_map_;
  std::unordered_map<GlobalImageId, escher::ImagePtr> render_target_map_;
  std::unordered_map<GlobalImageId, escher::TexturePtr> depth_target_map_;
  std::set<GlobalImageId> pending_textures_;
  std::set<GlobalImageId> pending_render_targets_;

  uint32_t frame_number_ = 0;
};

}  // namespace flatland

#endif  // SRC_UI_SCENIC_LIB_FLATLAND_RENDERER_VK_RENDERER_H_
