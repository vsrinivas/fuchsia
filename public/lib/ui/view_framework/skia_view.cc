// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ui/view_framework/skia_view.h"

namespace mozart {

SkiaView::SkiaView(ViewManagerPtr view_manager,
                   fidl::InterfaceRequest<ViewOwner> view_owner_request,
                   const std::string& label)
    : BaseView(std::move(view_manager), std::move(view_owner_request), label),
      canvas_cycler_(session()) {
  parent_node().AddChild(canvas_cycler_);
}

SkiaView::~SkiaView() = default;

SkCanvas* SkiaView::AcquireCanvas() {
  if (!has_logical_size() || !has_metrics())
    return nullptr;

  SkCanvas* canvas =
      canvas_cycler_.AcquireCanvas(logical_size().width, logical_size().height,
                                   metrics().scale_x, metrics().scale_y);
  if (!canvas)
    return canvas;

  canvas_cycler_.SetTranslation(logical_size().width * .5f,
                                logical_size().height * .5f, 0.f);
  return canvas;
}

void SkiaView::ReleaseAndSwapCanvas() {
  canvas_cycler_.ReleaseAndSwapCanvas();
}

}  // namespace mozart
