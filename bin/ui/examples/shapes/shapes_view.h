// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXAMPLES_UI_SHAPES_SHAPES_VIEW_H_
#define EXAMPLES_UI_SHAPES_SHAPES_VIEW_H_

#include "apps/mozart/lib/view_framework/base_view.h"
#include "apps/mozart/lib/view_framework/skia/skia_surface_holder.h"
#include "lib/ftl/macros.h"

class SkCanvas;

namespace examples {

class ShapesView : public mozart::BaseView {
 public:
  ShapesView(mojo::InterfaceHandle<mojo::ApplicationConnector> app_connector,
             mojo::InterfaceRequest<mozart::ViewOwner> view_owner_request);

  ~ShapesView() override;

 private:
  // |BaseView|:
  void OnDraw() override;

  void DrawContent(const mojo::Size& size, SkCanvas* canvas);

  FTL_DISALLOW_COPY_AND_ASSIGN(ShapesView);
};

}  // namespace examples

#endif  // EXAMPLES_UI_SHAPES_SHAPES_VIEW_H_
