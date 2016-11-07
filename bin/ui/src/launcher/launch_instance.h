// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_SRC_LAUNCHER_LAUNCH_INSTANCE_H_
#define APPS_MOZART_SRC_LAUNCHER_LAUNCH_INSTANCE_H_

#include <memory>

#include "apps/modular/services/application/application_controller.fidl.h"
#include "apps/mozart/services/composition/compositor.fidl.h"
#include "apps/mozart/services/input/input_events.fidl.h"
#include "apps/mozart/services/views/view_manager.fidl.h"
#include "apps/mozart/services/views/view_token.fidl.h"
#include "apps/mozart/src/input_reader/input_interpreter.h"
#include "apps/mozart/src/input_reader/input_reader.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/ftl/functional/closure.h"
#include "lib/ftl/macros.h"

namespace launcher {

class LauncherViewTree;

class LaunchInstance {
 public:
  LaunchInstance(mozart::Compositor* compositor,
                 mozart::ViewManager* view_manager,
                 mozart::ViewOwnerPtr view_owner,
                 modular::ApplicationControllerPtr controller,
                 const ftl::Closure& shutdown_callback);
  ~LaunchInstance();

  void Launch();

 private:
  void CheckInput();

  mozart::Compositor* const compositor_;
  mozart::ViewManager* const view_manager_;

  mozart::RendererPtr renderer_;

  mozart::PointF mouse_coordinates_;
  mozart::ViewOwnerPtr root_view_owner_;
  modular::ApplicationControllerPtr controller_;

  ftl::Closure shutdown_callback_;

  std::unique_ptr<LauncherViewTree> view_tree_;

  mozart::input::InputInterpreter input_interpreter_;
  mozart::input::InputReader input_reader_;

  FTL_DISALLOW_COPY_AND_ASSIGN(LaunchInstance);
};

}  // namespace launcher

#endif  // APPS_MOZART_SRC_LAUNCHER_LAUNCH_INSTANCE_H_
