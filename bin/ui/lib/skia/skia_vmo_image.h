// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_LIB_SKIA_SKIA_VMO_IMAGE_H_
#define APPS_MOZART_LIB_SKIA_SKIA_VMO_IMAGE_H_

#include <magenta/types.h>

#include "apps/mozart/services/composition/interfaces/image.mojom.h"
#include "third_party/skia/include/core/SkImage.h"
#include "third_party/skia/include/core/SkImageInfo.h"

namespace mozart {

// Makes an |SkImage| object backed by an image which is mapped read only.
// Returns nullptr on failure.
sk_sp<SkImage> MakeSkImage(ImagePtr image);

// Makes an |SkImage| object backed by a virtual memory object which is mapped
// read only.  Does not take ownership of the handle.
// Returns nullptr on failure.
sk_sp<SkImage> MakeSkImageFromVMO(mx_handle_t vmo,
                                  const SkImageInfo& info,
                                  size_t row_bytes);

}  // namespace mozart

#endif  // APPS_MOZART_LIB_SKIA_SKIA_VMO_IMAGE_H_
