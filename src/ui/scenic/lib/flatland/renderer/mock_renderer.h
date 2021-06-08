// Copyright 2021 The Fuchsia Authors. All rights reserved.
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
               bool(allocation::GlobalBufferCollectionId, fuchsia::sysmem::Allocator_Sync*,
                    fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken>));

  MOCK_METHOD1(ReleaseBufferCollection, void(allocation::GlobalBufferCollectionId));

  MOCK_METHOD1(ImportBufferImage, bool(const allocation::ImageMetadata&));

  MOCK_METHOD1(ReleaseBufferImage, void(allocation::GlobalImageId image_id));

  MOCK_METHOD3(RegisterRenderTargetCollection,
               bool(allocation::GlobalBufferCollectionId, fuchsia::sysmem::Allocator_Sync*,
                    fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken>));

  MOCK_METHOD1(DeregisterRenderTargetCollection, void(allocation::GlobalBufferCollectionId));

  MOCK_METHOD4(Render,
               void(const allocation::ImageMetadata&, const std::vector<Rectangle2D>&,
                    const std::vector<allocation::ImageMetadata>&, const std::vector<zx::event>&));

  MOCK_CONST_METHOD1(ChoosePreferredPixelFormat,
                     zx_pixel_format_t(const std::vector<zx_pixel_format_t>&));
};

}  // namespace flatland

#endif  // SRC_UI_SCENIC_LIB_FLATLAND_RENDERER_MOCK_RENDERER_H_
