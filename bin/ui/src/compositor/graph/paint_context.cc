// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/compositor/graph/paint_context.h"

#include "lib/ftl/logging.h"

namespace compositor {

PaintContext::PaintContext(SkCanvas* canvas) : canvas_(canvas) {
  FTL_DCHECK(canvas_);
}

PaintContext::~PaintContext() {}

void PaintContext::AddImage(ftl::RefPtr<RenderImage> image) {
  images_.insert(std::move(image));
}

PaintContext::ImageSet PaintContext::TakeImages() {
  return std::move(images_);
}

}  // namespace compositor
