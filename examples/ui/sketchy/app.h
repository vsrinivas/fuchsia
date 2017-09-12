// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/app/cpp/application_context.h"
#include "garnet/examples/ui/sketchy/scene.h"
#include "lib/ui/scenic/client/session.h"
#include "lib/ui/sketchy/canvas.h"
#include "lib/ui/scenic/fidl/display_info.fidl-common.h"
#include "lib/ui/scenic/fidl/scene_manager.fidl.h"

namespace sketchy_example {

using namespace sketchy_lib;

class App {
 public:
  App();

 private:
  // Called asynchronously by constructor.
  void Init(scenic::DisplayInfoPtr display_info);

  fsl::MessageLoop* const loop_;
  const std::unique_ptr<app::ApplicationContext> context_;
  scenic::SceneManagerPtr scene_manager_;
  const std::unique_ptr<scenic_lib::Session> session_;
  const std::unique_ptr<Canvas> canvas_;
  std::unique_ptr<Scene> scene_;
};

}  // namespace sketchy_example
