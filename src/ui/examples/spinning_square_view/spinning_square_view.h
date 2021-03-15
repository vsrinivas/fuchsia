// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_EXAMPLES_SPINNING_SQUARE_VIEW_SPINNING_SQUARE_VIEW_H_
#define SRC_UI_EXAMPLES_SPINNING_SQUARE_VIEW_SPINNING_SQUARE_VIEW_H_

#include <lib/syslog/cpp/macros.h>
#include <lib/ui/scenic/cpp/resources.h>

#include "src/lib/fxl/macros.h"
#include "src/lib/ui/base_view/base_view.h"

namespace examples {

class SpinningSquareView : public scenic::BaseView {
 public:
  explicit SpinningSquareView(scenic::ViewContext context);
  ~SpinningSquareView() override;

 private:
  // |scenic::BaseView|
  void OnSceneInvalidated(fuchsia::images::PresentationInfo presentation_info) override;

  // |scenic::SessionListener|
  void OnScenicError(std::string error) override { FX_LOGS(ERROR) << "Scenic Error " << error; }

  scenic::ShapeNode background_node_;
  scenic::ShapeNode square_node_;

  uint64_t start_time_ = 0u;

  FXL_DISALLOW_COPY_AND_ASSIGN(SpinningSquareView);
};

}  // namespace examples

#endif  // SRC_UI_EXAMPLES_SPINNING_SQUARE_VIEW_SPINNING_SQUARE_VIEW_H_
