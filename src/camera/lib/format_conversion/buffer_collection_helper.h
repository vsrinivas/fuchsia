// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_LIB_FORMAT_CONVERSION_BUFFER_COLLECTION_HELPER_H_
#define SRC_CAMERA_LIB_FORMAT_CONVERSION_BUFFER_COLLECTION_HELPER_H_

#include <fuchsia/sysmem/c/fidl.h>
#include <fuchsia/sysmem/cpp/fidl.h>

#include "format_conversion.h"

namespace camera {

// This class serves as a utility to convert |fuchsia::sysmem::BufferCollectionInfo_2| type
// to C types |fuchsia_sysmem_BufferCollectionInfo| & |fuchsia_sysmem_BufferCollectionInfo_2|
class BufferCollectionHelper {
 public:
  explicit BufferCollectionHelper(
      const fuchsia::sysmem::BufferCollectionInfo_2& hlcpp_buffer_collection) {
    ConvertToCTypeBufferCollectionInfo2(hlcpp_buffer_collection, &c_buffer_collection_);
  }

  fuchsia_sysmem_BufferCollectionInfo_2* GetC() { return &c_buffer_collection_; }

 private:
  fuchsia_sysmem_BufferCollectionInfo_2 c_buffer_collection_;
};

}  // namespace camera

#endif  // SRC_CAMERA_LIB_FORMAT_CONVERSION_BUFFER_COLLECTION_HELPER_H_
