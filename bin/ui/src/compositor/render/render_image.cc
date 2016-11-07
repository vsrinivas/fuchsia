// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/compositor/render/render_image.h"

#include "apps/mozart/lib/skia/skia_vmo_image.h"
#include "lib/ftl/logging.h"
#include "third_party/skia/include/core/SkImage.h"
#include "third_party/skia/include/core/SkPixmap.h"

namespace compositor {

RenderImage::RenderImage(const sk_sp<SkImage>& image) : image_(image) {
  FTL_DCHECK(image_);
}

RenderImage::~RenderImage() {}

ftl::RefPtr<RenderImage> RenderImage::CreateFromImage(mozart::ImagePtr image) {
  sk_sp<SkImage> sk_image = MakeSkImage(std::move(image));
  if (!sk_image)
    return nullptr;
  return ftl::MakeRefCounted<RenderImage>(std::move(sk_image));
}

}  // namespace compositor
