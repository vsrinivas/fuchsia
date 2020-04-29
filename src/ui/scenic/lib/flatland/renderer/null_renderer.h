// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_FLATLAND_RENDERER_NULL_RENDERER_H_
#define SRC_UI_SCENIC_LIB_FLATLAND_RENDERER_NULL_RENDERER_H_

#include <mutex>
#include <unordered_map>

#include "src/ui/scenic/lib/flatland/renderer/buffer_collection.h"
#include "src/ui/scenic/lib/flatland/renderer/renderer.h"

namespace flatland {

// A renderer implementation used for validation. It does everything a standard
// renderer implementation does except for actually rendering.
class NullRenderer : public Renderer {
 public:
  ~NullRenderer() override = default;

  // |Renderer|.
  GlobalBufferCollectionId RegisterBufferCollection(
      fuchsia::sysmem::Allocator_Sync* sysmem_allocator,
      fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token) override;

  // |Renderer|.
  std::optional<BufferCollectionMetadata> Validate(GlobalBufferCollectionId collection_id) override;

  // |Renderer|.
  void Render(const std::vector<ImageMetadata>& images) override;

 private:
  // This mutex is used to protect access to |collection_map_| and |collection_metadata_map_|.
  std::mutex lock_;
  std::unordered_map<GlobalBufferCollectionId, BufferCollectionInfo> collection_map_;
  std::unordered_map<GlobalBufferCollectionId, BufferCollectionMetadata> collection_metadata_map_;

  // Thread-safe identifier generator. Starts at 1 as 0 is an invalid ID.
  std::atomic<GlobalBufferCollectionId> id_generator_ = 1;
};

}  // namespace flatland

#endif  // SRC_UI_SCENIC_LIB_FLATLAND_RENDERER_NULL_RENDERER_H_
