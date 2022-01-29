// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/vk/color_space.h"

#include "src/ui/lib/escher/util/image_utils.h"

namespace escher {

ColorSpace GetDefaultColorSpace(vk::Format format) {
  if (image_utils::IsYuvFormat(format)) {
    return ColorSpace::kRec709;
  }
  return ColorSpace::kSrgb;
}

}  // namespace escher
