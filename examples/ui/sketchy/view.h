// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_UI_SKETCHY_VIEW_H_
#define GARNET_EXAMPLES_UI_SKETCHY_VIEW_H_

#include <lib/async-loop/cpp/loop.h>

#include "lib/component/cpp/startup_context.h"
#include "lib/fxl/macros.h"
#include "lib/ui/sketchy/client/canvas.h"
#include "lib/ui/sketchy/client/resources.h"
#include "lib/ui/view_framework/base_view.h"

namespace sketchy_example {

using namespace sketchy_lib;

// A view that allows user to draw strokes on the screen. Pressing 'c' would
// clear the canvas.
class View final : public mozart::BaseView {
 public:
  View(async::Loop* loop, component::StartupContext* startup_context,
       ::fuchsia::ui::viewsv1::ViewManagerPtr view_manager,
       fidl::InterfaceRequest<::fuchsia::ui::viewsv1token::ViewOwner>
           view_owner_request);

  ~View() override = default;

  // mozart::BaseView.
  void OnPropertiesChanged(
      ::fuchsia::ui::viewsv1::ViewProperties old_properties) override;
  bool OnInputEvent(fuchsia::ui::input::InputEvent event) override;

 private:
  Canvas canvas_;
  scenic::ShapeNode background_node_;
  scenic::EntityNode import_node_holder_;
  ImportNode import_node_;
  StrokeGroup scratch_group_;
  StrokeGroup stable_group_;
  std::map<uint32_t, StrokePtr> pointer_id_to_stroke_map_;

  FXL_DISALLOW_COPY_AND_ASSIGN(View);
};

}  // namespace sketchy_example

#endif  // GARNET_EXAMPLES_UI_SKETCHY_VIEW_H_
