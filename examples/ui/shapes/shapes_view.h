// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_EXAMPLES_SHAPES_SHAPES_VIEW_H_
#define APPS_MOZART_EXAMPLES_SHAPES_SHAPES_VIEW_H_

#include "lib/ui/scenic/client/resources.h"
#include "lib/ui/view_framework/base_view.h"
#include "lib/ftl/macros.h"

class SkCanvas;

namespace examples {

class ShapesView : public mozart::BaseView {
 public:
  ShapesView(mozart::ViewManagerPtr view_manager,
             fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request);

  ~ShapesView() override;

 private:
  // |BaseView|:
  void OnSceneInvalidated(
      scenic::PresentationInfoPtr presentation_info) override;

  scenic_lib::ShapeNode background_node_;
  scenic_lib::ShapeNode card_node_;
  scenic_lib::ShapeNode circle_node_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ShapesView);
};

}  // namespace examples

#endif  // APPS_MOZART_EXAMPLES_SHAPES_SHAPES_VIEW_H_
