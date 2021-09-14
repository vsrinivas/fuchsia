// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/bin/device/size_util.h"

namespace camera {

fuchsia::math::Size ConvertToSize(fuchsia::sysmem::ImageFormat_2 format) {
  ZX_DEBUG_ASSERT(format.coded_width < std::numeric_limits<int32_t>::max());
  ZX_DEBUG_ASSERT(format.coded_height < std::numeric_limits<int32_t>::max());
  return {.width = static_cast<int32_t>(format.coded_width),
          .height = static_cast<int32_t>(format.coded_height)};
}

bool SizeEqual(fuchsia::math::Size a, fuchsia::math::Size b) {
  return (a.width == b.width) && (a.height == b.height);
}

}  // namespace camera
