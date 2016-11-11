// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_LIB_SKIA_SKIA_VMO_IMAGE_H_
#define APPS_MOZART_LIB_SKIA_SKIA_VMO_IMAGE_H_

#include "apps/mozart/services/buffers/cpp/buffer_consumer.h"
#include "apps/mozart/services/images/image.fidl.h"
#include "third_party/skia/include/core/SkImage.h"
#include "third_party/skia/include/core/SkImageInfo.h"

namespace mozart {

// Creates an |SkImage| object backed by an image.
// The |consumer| must be configured to map buffers with read permission.
// On success, the buffer's fence is returned in |out_fence| if not null.
// Returns nullptr on failure.
// Releases the image buffer when the returned |SkImage| is destroyed.
sk_sp<SkImage> MakeSkImage(ImagePtr image,
                           BufferConsumer* consumer,
                           std::unique_ptr<BufferFence>* out_fence);

// Creates an |SkImage| object backed by a buffer with image info.
// The |consumer| must be configured to map buffers with read permission.
// On success, the buffer's fence is returned in |out_fence| if not null.
// Returns nullptr on failure.
// Releases the image buffer when the returned |SkImage| is destroyed.
sk_sp<SkImage> MakeSkImageFromBuffer(const SkImageInfo& info,
                                     size_t row_bytes,
                                     BufferPtr buffer,
                                     BufferConsumer* consumer,
                                     std::unique_ptr<BufferFence>* out_fence);

}  // namespace mozart

#endif  // APPS_MOZART_LIB_SKIA_SKIA_VMO_IMAGE_H_
