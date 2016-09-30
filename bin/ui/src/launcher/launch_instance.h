// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_SRC_LAUNCHER_LAUNCH_INSTANCE_H_
#define APPS_MOZART_SRC_LAUNCHER_LAUNCH_INSTANCE_H_

#include <memory>

#include "apps/mozart/services/composition/interfaces/compositor.mojom.h"
#include "apps/mozart/services/input/interfaces/input_events.mojom.h"
#include "apps/mozart/services/views/interfaces/view_manager.mojom.h"
#include "apps/mozart/services/views/interfaces/view_provider.mojom.h"
#include "apps/mozart/src/launcher/input/input_device.h"
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
                 mozart::ViewProviderPtr view_provider,
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
  mozart::ViewProviderPtr view_provider_;

  ftl::Closure shutdown_callback_;

  std::unique_ptr<LauncherViewTree> view_tree_;

  mozart::ViewOwnerPtr client_view_owner_;

  InputDeviceMonitor input_device_monitor_;

  FTL_DISALLOW_COPY_AND_ASSIGN(LaunchInstance);
};

}  // namespace launcher

#endif  // APPS_MOZART_SRC_LAUNCHER_LAUNCH_INSTANCE_H_
