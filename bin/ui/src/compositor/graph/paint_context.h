// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_SRC_COMPOSITOR_GRAPH_PAINT_CONTEXT_H_
#define APPS_MOZART_SRC_COMPOSITOR_GRAPH_PAINT_CONTEXT_H_

#include <unordered_set>

#include "apps/mozart/src/compositor/render/render_image.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/memory/ref_counted.h"

class SkCanvas;

namespace compositor {

class PaintContext {
 public:
  using ImageSet = std::unordered_set<ftl::RefPtr<RenderImage>>;

  explicit PaintContext(SkCanvas* canvas);
  ~PaintContext();

  SkCanvas* canvas() { return canvas_; }

  void AddImage(ftl::RefPtr<RenderImage> image);
  ImageSet TakeImages();

 private:
  SkCanvas* const canvas_;
  ImageSet images_;

  FTL_DISALLOW_COPY_AND_ASSIGN(PaintContext);
};

}  // namespace compositor

#endif  // APPS_MOZART_SRC_COMPOSITOR_GRAPH_PAINT_CONTEXT_H_
