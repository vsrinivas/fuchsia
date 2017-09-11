// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ui/scenic/skia/image_info.h"

#include "lib/fxl/logging.h"

namespace scenic_lib {
namespace skia {

SkImageInfo MakeSkImageInfo(const scenic::ImageInfo& image_info) {
  FXL_DCHECK(image_info.tiling == scenic::ImageInfo::Tiling::LINEAR);

  switch (image_info.pixel_format) {
    case scenic::ImageInfo::PixelFormat::BGRA_8:
      return SkImageInfo::Make(image_info.width, image_info.height,
                               kBGRA_8888_SkColorType, kOpaque_SkAlphaType);
  }

  FXL_NOTREACHED();
}

}  // namespace skia
}  // namespace scenic_lib
