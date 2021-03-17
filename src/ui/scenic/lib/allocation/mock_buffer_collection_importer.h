// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_ALLOCATION_MOCK_BUFFER_COLLECTION_IMPORTER_H_
#define SRC_UI_SCENIC_LIB_ALLOCATION_MOCK_BUFFER_COLLECTION_IMPORTER_H_

#include <gmock/gmock.h>

#include "src/ui/scenic/lib/allocation/buffer_collection_importer.h"

namespace allocation {

// Mock class of BufferCollectionImporter for API testing.
class MockBufferCollectionImporter : public BufferCollectionImporter {
 public:
  MOCK_METHOD3(ImportBufferCollection,
               bool(GlobalBufferCollectionId, fuchsia::sysmem::Allocator_Sync*,
                    fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken>));

  MOCK_METHOD1(ReleaseBufferCollection, void(GlobalBufferCollectionId));

  MOCK_METHOD1(ImportBufferImage, bool(const ImageMetadata&));

  MOCK_METHOD1(ReleaseBufferImage, void(GlobalImageId));
};

}  // namespace allocation

#endif  // SRC_UI_SCENIC_LIB_ALLOCATION_MOCK_BUFFER_COLLECTION_IMPORTER_H_
