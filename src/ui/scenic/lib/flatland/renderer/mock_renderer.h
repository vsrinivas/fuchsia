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
  MOCK_METHOD(bool, ImportBufferCollection,
              (allocation::GlobalBufferCollectionId, fuchsia::sysmem::Allocator_Sync*,
               fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken>));

  MOCK_METHOD(void, ReleaseBufferCollection, (allocation::GlobalBufferCollectionId));

  MOCK_METHOD(bool, ImportBufferImage, (const allocation::ImageMetadata&));

  MOCK_METHOD(void, ReleaseBufferImage, (allocation::GlobalImageId image_id));

  MOCK_METHOD(bool, RegisterRenderTargetCollection,
              (allocation::GlobalBufferCollectionId, fuchsia::sysmem::Allocator_Sync*,
               fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken>,
               fuchsia::math::SizeU size));

  MOCK_METHOD(void, DeregisterRenderTargetCollection, (allocation::GlobalBufferCollectionId));

  MOCK_METHOD(void, Render,
              (const allocation::ImageMetadata&, const std::vector<Rectangle2D>&,
               const std::vector<allocation::ImageMetadata>&, const std::vector<zx::event>&));

  MOCK_METHOD(zx_pixel_format_t, ChoosePreferredPixelFormat,
              (const std::vector<zx_pixel_format_t>&), (const));
};

}  // namespace flatland

#endif  // SRC_UI_SCENIC_LIB_FLATLAND_RENDERER_MOCK_RENDERER_H_
