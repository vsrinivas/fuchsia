// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_UI_SPINNING_SQUARE_SPINNING_SQUARE_VIEW_H_
#define GARNET_EXAMPLES_UI_SPINNING_SQUARE_SPINNING_SQUARE_VIEW_H_

#include "lib/fxl/macros.h"
#include "lib/ui/scenic/cpp/resources.h"
#include "lib/ui/view_framework/base_view.h"

namespace examples {

class SpinningSquareView : public mozart::BaseView {
 public:
  SpinningSquareView(
      ::fuchsia::ui::views_v1::ViewManagerPtr view_manager,
      fidl::InterfaceRequest<::fuchsia::ui::views_v1_token::ViewOwner> view_owner_request);
  ~SpinningSquareView() override;

 private:
  // |BaseView|:
  void OnSceneInvalidated(fuchsia::images::PresentationInfo presentation_info) override;

  scenic::ShapeNode background_node_;
  scenic::ShapeNode square_node_;

  uint64_t start_time_ = 0u;

  FXL_DISALLOW_COPY_AND_ASSIGN(SpinningSquareView);
};

}  // namespace examples

#endif  // GARNET_EXAMPLES_UI_SPINNING_SQUARE_SPINNING_SQUARE_VIEW_H_
