// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_UI_SKETCHY_VIEW_H_
#define GARNET_EXAMPLES_UI_SKETCHY_VIEW_H_

#include <lib/async-loop/cpp/loop.h>

#include "lib/component/cpp/startup_context.h"
#include "src/lib/fxl/macros.h"
#include "lib/ui/base_view/cpp/v1_base_view.h"
#include "lib/ui/sketchy/client/canvas.h"
#include "lib/ui/sketchy/client/resources.h"

namespace sketchy_example {

// A view that allows user to draw strokes on the screen. Pressing 'c' would
// clear the canvas.
class View final : public scenic::BaseView {
 public:
  View(scenic::ViewContext context, async::Loop* loop);

  ~View() override = default;

  // |scenic::BaseView|
  void OnPropertiesChanged(
      fuchsia::ui::gfx::ViewProperties old_properties) override;

  // |scenic::BaseView|
  void OnInputEvent(fuchsia::ui::input::InputEvent event) override;

  // |scenic::SessionListener|
  void OnScenicError(std::string error) override {
    FXL_LOG(ERROR) << "Scenic Error " << error;
  }

 private:
  sketchy_lib::Canvas canvas_;
  scenic::ShapeNode background_node_;
  scenic::EntityNode import_node_holder_;
  sketchy_lib::ImportNode import_node_;
  sketchy_lib::StrokeGroup scratch_group_;
  sketchy_lib::StrokeGroup stable_group_;
  std::map<uint32_t, sketchy_lib::StrokePtr> pointer_id_to_stroke_map_;

  FXL_DISALLOW_COPY_AND_ASSIGN(View);
};

}  // namespace sketchy_example

#endif  // GARNET_EXAMPLES_UI_SKETCHY_VIEW_H_
