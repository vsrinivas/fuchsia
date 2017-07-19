// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/mozart/services/images/image_info.fidl.h"
#include "third_party/skia/include/core/SkImageInfo.h"

namespace mozart {
namespace skia {

// Creates Skia image information from a |mozart2::ImageInfo| object.
SkImageInfo MakeSkImageInfo(const mozart2::ImageInfo& image_info);

}  // namespace skia
}  // namespace mozart
