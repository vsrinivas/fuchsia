// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_UI_SHAPES_SHAPES_VIEW_H_
#define GARNET_EXAMPLES_UI_SHAPES_SHAPES_VIEW_H_

#include "lib/fxl/macros.h"
#include "lib/ui/base_view/cpp/v1_base_view.h"
#include "lib/ui/scenic/cpp/resources.h"

class SkCanvas;

namespace examples {

class ShapesView : public scenic::V1BaseView {
 public:
  ShapesView(scenic::ViewContext context);

  ~ShapesView() override;

 private:
  // | scenic::V1BaseView |
  void OnSceneInvalidated(
      fuchsia::images::PresentationInfo presentation_info) override;

  scenic::ShapeNode background_node_;
  scenic::ShapeNode card_node_;
  scenic::ShapeNode circle_node_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ShapesView);
};

}  // namespace examples

#endif  // GARNET_EXAMPLES_UI_SHAPES_SHAPES_VIEW_H_
