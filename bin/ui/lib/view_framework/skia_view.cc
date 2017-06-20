// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/lib/view_framework/skia_view.h"

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
  if (!has_size())
    return nullptr;

  canvas_cycler_.SetTranslation(
      (float[]){size().width * .5f, size().height * .5f, 0u});
  return canvas_cycler_.AcquireCanvas(size().width, size().height);
}

void SkiaView::ReleaseAndSwapCanvas() {
  canvas_cycler_.ReleaseAndSwapCanvas();
}

}  // namespace mozart
