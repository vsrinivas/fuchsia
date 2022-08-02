// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_CAMERA_LIB_RAW_FORMATS_RAW_LOOKUPS_H_
#define SRC_CAMERA_LIB_RAW_FORMATS_RAW_LOOKUPS_H_

#include "src/camera/lib/raw_formats/raw.h"

namespace camera::raw {

/* Given a raw format instance, get the value of the requested pixel in the provided buffer.

   NOTE: This function exists for two reasons:
   1) To provide a reference implementation for pixel lookups using the RawFormat datastructures.
   2) To provide a function which can be used to check the contents of buffers as part of tests.

   This function should not be used "in production" to do pixel value lookups (and especially not
   large numbers of pixel value lookups). Hardware accelerated means of operating on image buffers
   should always be preferred when available. If no hardware specifically designed for the required
   image processing exists, consider implementations which use compiler intrinsics to leverage
   things like SSE or AVX.
 */
uint64_t GetPixel(const RawFormatInstance& format_instance, uint32_t pixel_index,
                  const uint8_t* buffer, size_t buffer_size);

/* Given a raw format instance, set the value of the requested pixel in the provided buffer.

   NOTE: This function exists for two reasons:
   1) To provide a reference implementation for pixel setting using the RawFormat datastructures.
   2) To provide a function which can be used to set the contents of buffers as part of tests.

   This function should not be used "in production" to do pixel value setting (and especially not
   setting large numbers of pixels). Hardware accelerated means of operating on image buffers
   should always be preferred when available. If no hardware specifically designed for the required
   image processing exists, consider implementations which use compiler intrinsics to leverage
   things like SSE or AVX.
 */
void SetPixel(const RawFormatInstance& format_instance, uint32_t pixel_index, uint64_t pixel_value,
              uint8_t* buffer, size_t buffer_size);

}  // namespace camera::raw

#endif  // SRC_CAMERA_LIB_RAW_FORMATS_RAW_LOOKUPS_H_
