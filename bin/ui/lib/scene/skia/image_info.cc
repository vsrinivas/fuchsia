// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/lib/scene/skia/image_info.h"

#include "lib/ftl/logging.h"

namespace mozart {
namespace skia {

SkImageInfo MakeSkImageInfo(const mozart2::ImageInfo& image_info) {
  FTL_DCHECK(image_info.tiling == mozart2::ImageInfo::Tiling::LINEAR);

  switch (image_info.pixel_format) {
    case mozart2::ImageInfo::PixelFormat::BGRA_8:
      return SkImageInfo::Make(image_info.width, image_info.height,
                               kBGRA_8888_SkColorType, kOpaque_SkAlphaType);
  }

  FTL_NOTREACHED();
}

}  // namespace skia
}  // namespace mozart
