// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_FLATLAND_BUFFER_COLLECTION_IMPORTER_H_
#define SRC_UI_SCENIC_LIB_FLATLAND_BUFFER_COLLECTION_IMPORTER_H_

#include "src/ui/scenic/lib/flatland/renderer/renderer.h"

namespace flatland {

// This interface is used for importing Flatland buffer collections
// and images to external services that would like to also have access
// to the collection and set their own constraints. This interface allows
// Flatland to remain agnostic as to the implementation details of a
// particular service.
class BufferCollectionImporter {
 public:
  // Allows the service to set its own constraints on the buffer collection. Must be set before
  // the buffer collection is fully allocated/validated.
  virtual void ImportBufferCollection(
      sysmem_util::GlobalBufferCollectionId collection_id,
      fuchsia::sysmem::Allocator_Sync* sysmem_allocator,
      fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token) = 0;

  // Deregisters the buffer collection from the service. All images associated with the buffer
  // collection referenced by |collection_id| should be released via calls to |ReleaseImage|
  // before the buffer collection itself is released.
  virtual void ReleaseBufferCollection(sysmem_util::GlobalBufferCollectionId collection_id) = 0;

  // Has the service create an image for itself from the provided buffer collection.
  virtual void ImportImage(const ImageMetadata& meta_data) = 0;

  // Deregisters the provided image from the service.
  virtual void ReleaseImage(GlobalImageId image_id) = 0;

  virtual ~BufferCollectionImporter() = default;
};

}  // namespace flatland

#endif  // SRC_UI_SCENIC_LIB_FLATLAND_BUFFER_COLLECTION_IMPORTER_H_
