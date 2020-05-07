// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_FLATLAND_RENDERER_VK_RENDERER_H_
#define SRC_UI_SCENIC_LIB_FLATLAND_RENDERER_VK_RENDERER_H_

#include <unordered_map>

#include "src/ui/lib/escher/flatland/rectangle_compositor.h"
#include "src/ui/scenic/lib/flatland/renderer/gpu_mem.h"
#include "src/ui/scenic/lib/flatland/renderer/renderer.h"

namespace flatland {

// Implementation of the Flatland Renderer interface that relies on Escher and
// by extension the Vulkan API.
class VkRenderer final : public Renderer {
 public:
  VkRenderer(escher::EscherWeakPtr escher);
  ~VkRenderer() override;

  // |Renderer|.
  GlobalBufferCollectionId RegisterBufferCollection(
      fuchsia::sysmem::Allocator_Sync* sysmem_allocator,
      fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token) override;

  // |Renderer|.
  std::optional<BufferCollectionMetadata> Validate(GlobalBufferCollectionId collection_id) override;

  // |Renderer|.
  void Render(const ImageMetadata& render_target,
              const std::vector<RenderableMetadata>& renderables) override;

 private:
  // Vulkan rendering components.
  escher::EscherWeakPtr escher_;
  std::unique_ptr<escher::RectangleCompositor> compositor_;

  // This mutex is used to protect access to |collection_map_|, |collection_metadata_map_|,
  // and |vk_collection_map_|.
  std::mutex lock_;
  std::unordered_map<GlobalBufferCollectionId, BufferCollectionInfo> collection_map_;
  std::unordered_map<GlobalBufferCollectionId, BufferCollectionMetadata> collection_metadata_map_;
  std::unordered_map<GlobalBufferCollectionId, vk::BufferCollectionFUCHSIA> vk_collection_map_;

  // Thread-safe identifier generator. Starts at 1 as 0 is an invalid ID.
  std::atomic<GlobalBufferCollectionId> id_generator_ = 1;
};

}  // namespace flatland

#endif  // SRC_UI_SCENIC_LIB_FLATLAND_RENDERER_VK_RENDERER_H_
