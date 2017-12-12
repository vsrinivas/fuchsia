// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_UI_SKETCHY_APP_H_
#define GARNET_EXAMPLES_UI_SKETCHY_APP_H_

#include "garnet/examples/ui/sketchy/scene.h"
#include "lib/app/cpp/application_context.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/ui/scenic/client/session.h"
#include "lib/ui/scenic/fidl/display_info.fidl-common.h"
#include "lib/ui/scenic/fidl/scene_manager.fidl.h"
#include "lib/ui/sketchy/canvas.h"

namespace sketchy_example {

using namespace sketchy_lib;

class App {
 public:
  App();

 private:
  // Called asynchronously by constructor.
  void Init(scenic::DisplayInfoPtr display_info);
  // Canvas callback for animation
  void CanvasCallback(scenic::PresentationInfoPtr info);

  fsl::MessageLoop* const loop_;
  const std::unique_ptr<app::ApplicationContext> context_;
  scenic::SceneManagerPtr scene_manager_;
  const std::unique_ptr<scenic_lib::Session> session_;
  const std::unique_ptr<Canvas> canvas_;
  std::unique_ptr<Scene> scene_;

  // Animated stroke for demo
  std::unique_ptr<ImportNode> import_node_;
  bool is_animated_stroke_at_top_ = true;
  std::unique_ptr<Stroke> animated_stroke_;
  StrokePath animated_path_at_top_;
  StrokePath animated_path_at_bottom_;
};

}  // namespace sketchy_example

#endif  // GARNET_EXAMPLES_UI_SKETCHY_APP_H_
