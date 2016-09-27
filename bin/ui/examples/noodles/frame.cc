// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/examples/noodles/frame.h"

#include "lib/ftl/logging.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkPicture.h"

namespace examples {

Frame::Frame(const mojo::Size& size,
             sk_sp<SkPicture> picture,
             mojo::gfx::composition::SceneMetadataPtr scene_metadata)
    : size_(size), picture_(picture), scene_metadata_(scene_metadata.Pass()) {
  FTL_DCHECK(picture_);
}

Frame::~Frame() {}

void Frame::Paint(SkCanvas* canvas) {
  FTL_DCHECK(canvas);

  canvas->clear(SK_ColorBLACK);
  canvas->drawPicture(picture_.get());
  canvas->flush();
}

}  // namespace examples
