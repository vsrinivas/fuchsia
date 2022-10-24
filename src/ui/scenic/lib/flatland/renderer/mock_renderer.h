// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_FLATLAND_RENDERER_MOCK_RENDERER_H_
#define SRC_UI_SCENIC_LIB_FLATLAND_RENDERER_MOCK_RENDERER_H_

#include <gmock/gmock.h>

#include "src/ui/scenic/lib/allocation/buffer_collection_importer.h"
#include "src/ui/scenic/lib/flatland/renderer/renderer.h"

namespace flatland {

// Mock class of the Flatland Renderer for API testing.
class MockRenderer : public Renderer {
 public:
  MOCK_METHOD(bool, ImportBufferCollection,
              (allocation::GlobalBufferCollectionId, fuchsia::sysmem::Allocator_Sync*,
               fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken>,
               allocation::BufferCollectionUsage, std::optional<fuchsia::math::SizeU> size));

  MOCK_METHOD(void, ReleaseBufferCollection,
              (allocation::GlobalBufferCollectionId, allocation::BufferCollectionUsage));

  MOCK_METHOD(bool, ImportBufferImage,
              (const allocation::ImageMetadata&, allocation::BufferCollectionUsage));

  MOCK_METHOD(void, ReleaseBufferImage, (allocation::GlobalImageId image_id));

  MOCK_METHOD(void, Render,
              (const allocation::ImageMetadata&, const std::vector<Rectangle2D>&,
               const std::vector<allocation::ImageMetadata>&, const std::vector<zx::event>&, bool));

  MOCK_METHOD(void, SetColorConversionValues,
              ((const std::array<float, 9>&), (const std::array<float, 3>&),
               (const std::array<float, 3>&)));

  MOCK_METHOD(zx_pixel_format_t, ChoosePreferredPixelFormat,
              (const std::vector<zx_pixel_format_t>&), (const));

  MOCK_METHOD(bool, SupportsRenderInProtected, (), (const));

  MOCK_METHOD(bool, RequiresRenderInProtected, (const std::vector<allocation::ImageMetadata>&),
              (const));
};

}  // namespace flatland

#endif  // SRC_UI_SCENIC_LIB_FLATLAND_RENDERER_MOCK_RENDERER_H_
