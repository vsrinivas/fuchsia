// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_SRC_LAUNCHER_LAUNCH_INSTANCE_H_
#define APPS_MOZART_SRC_LAUNCHER_LAUNCH_INSTANCE_H_

#include <memory>

#include "apps/mozart/services/composition/interfaces/compositor.mojom.h"
#include "apps/mozart/services/input/interfaces/input_events.mojom.h"
#include "apps/mozart/services/views/interfaces/view_manager.mojom.h"
#include "apps/mozart/services/views/interfaces/view_token.mojom.h"
#include "apps/mozart/src/input_reader/input_interpreter.h"
#include "apps/mozart/src/input_reader/input_reader.h"
#include "lib/ftl/functional/closure.h"
#include "lib/ftl/macros.h"
#include "mojo/public/cpp/bindings/binding.h"
#include "mojo/services/framebuffer/interfaces/framebuffer.mojom.h"

namespace launcher {

class LauncherViewTree;

class LaunchInstance {
 public:
  LaunchInstance(mozart::Compositor* compositor,
                 mozart::ViewManager* view_manager,
                 mojo::InterfaceHandle<mojo::Framebuffer> framebuffer,
                 mojo::FramebufferInfoPtr framebuffer_info,
                 mozart::ViewOwnerPtr view_owner,
                 const ftl::Closure& shutdown_callback);
  ~LaunchInstance();

  void Launch();

 private:
  void CheckInput();

  mozart::Compositor* const compositor_;
  mozart::ViewManager* const view_manager_;

  mojo::InterfaceHandle<mojo::Framebuffer> framebuffer_;
  mojo::FramebufferInfoPtr framebuffer_info_;
  mojo::Size framebuffer_size_;
  mojo::PointF mouse_coordinates_;
  mozart::ViewOwnerPtr root_view_owner_;

  ftl::Closure shutdown_callback_;

  std::unique_ptr<LauncherViewTree> view_tree_;

  mozart::input::InputInterpreter input_interpreter_;
  mozart::input::InputReader input_reader_;

  FTL_DISALLOW_COPY_AND_ASSIGN(LaunchInstance);
};

}  // namespace launcher

#endif  // APPS_MOZART_SRC_LAUNCHER_LAUNCH_INSTANCE_H_
