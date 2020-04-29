// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_FLATLAND_TESTS_MOCK_RENDERER_H_
#define SRC_UI_SCENIC_LIB_FLATLAND_TESTS_MOCK_RENDERER_H_

#include <gmock/gmock.h>

#include "src/ui/scenic/lib/flatland/renderer/renderer.h"

namespace flatland {

// Mock class of Renderer for Flatland API testing.
class MockRenderer : public Renderer {
 public:
  MOCK_METHOD2(RegisterBufferCollection,
               GlobalBufferCollectionId(
                   fuchsia::sysmem::Allocator_Sync* sysmem_allocator,
                   fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token));
  MOCK_METHOD1(Validate,
               std::optional<BufferCollectionMetadata>(GlobalBufferCollectionId collection_id));
  MOCK_METHOD1(Render, void(const std::vector<ImageMetadata>& images));
};

}  // namespace flatland

#endif  // SRC_UI_SCENIC_LIB_FLATLAND_TESTS_MOCK_RENDERER_H_
