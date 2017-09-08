// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/mozart/services/images/image_info.fidl.h"
#include "third_party/skia/include/core/SkImageInfo.h"

namespace scenic_lib {
namespace skia {

// Creates Skia image information from a |scenic::ImageInfo| object.
SkImageInfo MakeSkImageInfo(const scenic::ImageInfo& image_info);

}  // namespace skia
}  // namespace scenic_lib
