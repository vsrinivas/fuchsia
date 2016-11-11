// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_LIB_SKIA_SKIA_VMO_SURFACE_H_
#define APPS_MOZART_LIB_SKIA_SKIA_VMO_SURFACE_H_

#include <mx/vmo.h>

#include "apps/mozart/services/buffers/cpp/buffer_producer.h"
#include "apps/mozart/services/images/image.fidl.h"
#include "third_party/skia/include/core/SkImageInfo.h"
#include "third_party/skia/include/core/SkSize.h"
#include "third_party/skia/include/core/SkSurface.h"

namespace mozart {

// Creates a new |SkSurface| backed by an image using native pixel format.
// The |producer| must be configured to map buffers with read/write permission.
// Returns nullptr on failure.
// Signals the image buffer's fence when the returned |SkSurface| is destroyed.
sk_sp<SkSurface> MakeSkSurface(const SkISize& size,
                               BufferProducer* producer,
                               ImagePtr* out_image);

// Creates a new |SkSurface| backed by an image using native pixel format.
// The |producer| must be configured to map buffers with read/write permission.
// Returns nullptr on failure.
// Signals the image buffer's fence when the returned |SkSurface| is destroyed.
sk_sp<SkSurface> MakeSkSurface(const Size& size,
                               BufferProducer* producer,
                               ImagePtr* out_image);

// Creates a new |SkSurface| backed by an image using the specified |info|.
// The |producer| must be configured to map buffers with read/write permission.
// Returns nullptr on failure.
// Signals the image buffer's fence when the returned |SkSurface| is destroyed.
sk_sp<SkSurface> MakeSkSurface(const SkImageInfo& info,
                               BufferProducer* producer,
                               ImagePtr* out_image);

// Creates a new |SkSurface| backed by a VMO.
// Does not take ownership of the VMO.
// Returns nullptr on failure.
sk_sp<SkSurface> MakeSkSurfaceFromVMO(const SkImageInfo& info,
                                      size_t row_bytes,
                                      const mx::vmo& vmo);

}  // namespace mozart

#endif  // APPS_MOZART_LIB_SKIA_SKIA_VMO_SURFACE_H_
