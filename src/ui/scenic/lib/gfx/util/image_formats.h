// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_UTIL_IMAGE_FORMATS_H_
#define SRC_UI_SCENIC_LIB_GFX_UTIL_IMAGE_FORMATS_H_

#include <fuchsia/images/cpp/fidl.h>

#include "src/ui/lib/escher/util/image_utils.h"

// Contains utilities for converting from various formats to BGRA_8, which is
// what is needed to render.
// TODO(fxbug.dev/23774): Merge with existing image conversion libraries in media:
// bin/media/video/video_converter.h

namespace scenic_impl {
namespace gfx {
namespace image_formats {

// Returns a function that can be used to convert any format supported in
// ImageInfo into a BGRA_8 image.
escher::image_utils::ImageConversionFunction GetFunctionToConvertToBgra8(
    const ::fuchsia::images::ImageInfo& image_info);

}  // namespace image_formats
}  // namespace gfx
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_GFX_UTIL_IMAGE_FORMATS_H_
