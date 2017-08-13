// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "application/lib/app/application_context.h"
#include "apps/mozart/examples/sketchy/scene.h"
#include "apps/mozart/lib/scene/client/session.h"
#include "apps/mozart/lib/sketchy/canvas.h"
#include "apps/mozart/services/scene/display_info.fidl-common.h"
#include "apps/mozart/services/scene/scene_manager.fidl.h"

namespace sketchy_example {

using namespace sketchy_lib;

class App {
 public:
  App();

 private:
  // Called asynchronously by constructor.
  void Init(mozart2::DisplayInfoPtr display_info);

  mtl::MessageLoop* const loop_;
  const std::unique_ptr<app::ApplicationContext> context_;
  mozart2::SceneManagerPtr scene_manager_;
  const std::unique_ptr<mozart::client::Session> session_;
  const std::unique_ptr<Canvas> canvas_;
  std::unique_ptr<Scene> scene_;
};

}  // namespace sketchy_example
