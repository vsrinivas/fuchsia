// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_FLATLAND_RENDERER_MOCK_RENDERER_H_
#define SRC_UI_SCENIC_LIB_FLATLAND_RENDERER_MOCK_RENDERER_H_

#include <gmock/gmock.h>

#include "src/ui/scenic/lib/flatland/renderer/renderer.h"

namespace flatland {

// Mock class of the Flatland Renderer for API testing.
class MockRenderer : public Renderer {
 public:
  MOCK_METHOD3(ImportBufferCollection,
               bool(sysmem_util::GlobalBufferCollectionId, fuchsia::sysmem::Allocator_Sync*,
                    fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken>));

  MOCK_METHOD1(ReleaseBufferCollection, void(sysmem_util::GlobalBufferCollectionId));

  MOCK_METHOD1(ImportBufferImage, bool(const ImageMetadata&));

  MOCK_METHOD1(ReleaseBufferImage, void(sysmem_util::GlobalImageId image_id));

  MOCK_METHOD3(RegisterRenderTargetCollection,
               bool(sysmem_util::GlobalBufferCollectionId, fuchsia::sysmem::Allocator_Sync*,
                    fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken>));

  MOCK_METHOD1(DeregisterRenderTargetCollection, void(sysmem_util::GlobalBufferCollectionId));

  MOCK_METHOD4(Render, void(const ImageMetadata&, const std::vector<Rectangle2D>&,
                            const std::vector<ImageMetadata>&, const std::vector<zx::event>&));
};

}  // namespace flatland

#endif  // SRC_UI_SCENIC_LIB_FLATLAND_RENDERER_MOCK_RENDERER_H_
