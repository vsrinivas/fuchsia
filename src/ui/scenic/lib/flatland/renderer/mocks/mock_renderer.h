// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_FLATLAND_RENDERER_MOCKS_MOCK_RENDERER_H_
#define SRC_UI_SCENIC_LIB_FLATLAND_RENDERER_MOCKS_MOCK_RENDERER_H_

#include <gmock/gmock.h>

#include "src/ui/scenic/lib/flatland/renderer/renderer.h"

namespace flatland {

// Mock class of Renderer for Flatland API testing.
class MockRenderer : public Renderer {
 public:
  MOCK_METHOD3(RegisterRenderTargetCollection,
               bool(sysmem_util::GlobalBufferCollectionId collection_id,
                    fuchsia::sysmem::Allocator_Sync* sysmem_allocator,
                    fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token));

  MOCK_METHOD4(Render,
               void(const ImageMetadata& render_target, const std::vector<Rectangle2D>& rectangles,
                    const std::vector<ImageMetadata>& images,
                    const std::vector<zx::event>& release_fences));
};

}  // namespace flatland

#endif  // SRC_UI_SCENIC_LIB_FLATLAND_RENDERER_MOCKS_MOCK_RENDERER_H_
