// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_FLATLAND_TESTS_MOCK_BUFFER_COLLECTION_IMPORTER_H_
#define SRC_UI_SCENIC_LIB_FLATLAND_TESTS_MOCK_BUFFER_COLLECTION_IMPORTER_H_

#include <gmock/gmock.h>

#include "src/ui/scenic/lib/flatland/buffer_collection_importer.h"

namespace flatland {

// Mock class of BufferCollectionImporter for Flatland API testing.
class MockBufferCollectionImporter : public BufferCollectionImporter {
 public:
  MOCK_METHOD3(ImportBufferCollection,
               void(sysmem_util::GlobalBufferCollectionId, fuchsia::sysmem::Allocator_Sync*,
                    fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken>));

  MOCK_METHOD1(ReleaseBufferCollection, void(sysmem_util::GlobalBufferCollectionId));

  MOCK_METHOD1(ImportImage, void(const ImageMetadata&));

  MOCK_METHOD1(ReleaseImage, void(GlobalImageId));
};

}  // namespace flatland

#endif  // SRC_UI_SCENIC_LIB_FLATLAND_TESTS_MOCK_BUFFER_COLLECTION_IMPORTER_H_
