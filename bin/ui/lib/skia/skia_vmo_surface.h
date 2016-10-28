// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_LIB_SKIA_SKIA_VMO_SURFACE_H_
#define APPS_MOZART_LIB_SKIA_SKIA_VMO_SURFACE_H_

#include <magenta/types.h>
#include <mx/vmo.h>

#include "apps/mozart/services/composition/interfaces/image.mojom.h"
#include "third_party/skia/include/core/SkImageInfo.h"
#include "third_party/skia/include/core/SkSurface.h"
#include "third_party/skia/include/core/SkSize.h"

namespace mozart {

// Creates a new |SkSurface| backed by an image which is mapped read/write.
// Returns nullptr on failure.
sk_sp<SkSurface> MakeSkSurface(const SkImageInfo& info, ImagePtr* out_image);

// Creates a new |SkSurface| backed by an image which is mapped read/write.
// Uses native pixel format and given size.
// Returns nullptr on failure.
sk_sp<SkSurface> MakeSkSurface(const SkISize& size, ImagePtr* out_image);

// Creates a new |SkSurface| backed by an image which is mapped read/write.
// Uses native pixel format and given size.
// Returns nullptr on failure.
sk_sp<SkSurface> MakeSkSurface(const mojo::Size& size, ImagePtr* out_image);

// Makes an |SkSurface| object backed by a virtual memory object which is
// mapped read/write.  Does not take ownership of the handle.
// Returns nullptr on failure.
sk_sp<SkSurface> MakeSkSurfaceFromVMO(const mx::vmo& vmo,
                                      const SkImageInfo& info,
                                      size_t row_bytes);

}  // namespace mozart

#endif  // APPS_MOZART_LIB_SKIA_SKIA_VMO_SURFACE_H_
