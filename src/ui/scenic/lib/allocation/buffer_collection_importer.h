// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_ALLOCATION_BUFFER_COLLECTION_IMPORTER_H_
#define SRC_UI_SCENIC_LIB_ALLOCATION_BUFFER_COLLECTION_IMPORTER_H_

#include <fuchsia/sysmem/cpp/fidl.h>

#include "src/ui/scenic/lib/allocation/id.h"

namespace allocation {

// Struct representing the data needed to extract an image from a buffer collection.
// All pixel information is stored within the Vmo of the collection so this struct
// only needs information regarding which collection and which vmo to point to, and
// the overall size of the image. Only supports fuchsia::sysmem::PixelFormatType::BGRA32
// as the image format type.
struct ImageMetadata {
  // The unique id of the buffer collection this image is backed by.
  GlobalBufferCollectionId collection_id = kInvalidId;

  // The unique ID for this particular image.
  GlobalImageId identifier = kInvalidImageId;

  // A single buffer collection may have several vmos. This tells the importer
  // which vmo in the collection specified by |collection_id| to use as the memory
  // for this image. This value must be less than the total number of vmos of the
  // buffer collection we are constructing the image from.
  uint32_t vmo_index;

  // The dimensions of the image in pixels.
  uint32_t width = 0;
  uint32_t height = 0;

  // Linear-space RGBA values to multiply with the pixel values of the image.
  std::array<float, 4> multiply_color = {1.f, 1.f, 1.f, 1.f};

  // If false, the image will be rendered with translucency, taking into account both
  // the individual pixel alpha and the alpha of the |multiply_color| field multiplied
  // together.
  bool is_opaque = true;

  bool operator==(const ImageMetadata& meta) const {
    return (collection_id == meta.collection_id && vmo_index == meta.vmo_index &&
            width == meta.width && height == meta.height && is_opaque == meta.is_opaque &&
            multiply_color == meta.multiply_color);
  }
};

inline std::ostream& operator<<(std::ostream& str, const ImageMetadata& m) {
  str << "size=" << m.width << "x" << m.height << "  is_opaque=" << m.is_opaque
      << "  multiply_color=(" << m.multiply_color[0] << "," << m.multiply_color[1] << ","
      << m.multiply_color[2] << "," << m.multiply_color[3] << ")";
  return str;
}

// This interface is used for importing Flatland buffer collections and images to external services
// that would like to also have access to the collection and set their own constraints. This
// interface allows Flatland to remain agnostic as to the implementation details of a
class BufferCollectionImporter {
 public:
  // Allows the service to set its own constraints on the buffer collection. Must be set before
  // the buffer collection is fully allocated/validated. The return value indicates successful
  // importation via |true| and a failed importation via |false|. The collection_id can be
  // reused if the importation fails.
  virtual bool ImportBufferCollection(
      GlobalBufferCollectionId collection_id, fuchsia::sysmem::Allocator_Sync* sysmem_allocator,
      fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token) = 0;

  // Releases the buffer collection from the service. It may be called while there are associated
  // Images alive.
  virtual void ReleaseBufferCollection(GlobalBufferCollectionId collection_id) = 0;

  // Has the service create an image for itself from the provided buffer collection. Returns
  // true upon a successful import and false otherwise.
  //
  // TODO(62240): Give more detailed errors.
  virtual bool ImportBufferImage(const ImageMetadata& metadata) = 0;

  // Releases the provided image from the service.
  virtual void ReleaseBufferImage(GlobalImageId image_id) = 0;

  virtual ~BufferCollectionImporter() = default;
};

}  // namespace allocation

#endif  // SRC_UI_SCENIC_LIB_ALLOCATION_BUFFER_COLLECTION_IMPORTER_H_
