// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_UI_LAUNCHER_LAUNCH_INSTANCE_H_
#define SERVICES_UI_LAUNCHER_LAUNCH_INSTANCE_H_

#include <memory>

#include "apps/mozart/services/composition/interfaces/compositor.mojom.h"
#include "apps/mozart/services/input/interfaces/input_events.mojom.h"
#include "apps/mozart/services/views/interfaces/view_manager.mojom.h"
#include "apps/mozart/services/views/interfaces/view_provider.mojom.h"
#include "lib/ftl/functional/closure.h"
#include "lib/ftl/macros.h"
#include "mojo/public/cpp/bindings/binding.h"
#include "mojo/services/framebuffer/interfaces/framebuffer.mojom.h"

namespace launcher {

class LauncherViewTree;

class LaunchInstance {
 public:
  LaunchInstance(mojo::gfx::composition::Compositor* compositor,
                 mojo::ui::ViewManager* view_manager,
                 mojo::InterfaceHandle<mojo::Framebuffer> framebuffer,
                 mojo::FramebufferInfoPtr framebuffer_info,
                 mojo::ui::ViewProviderPtr view_provider,
                 const ftl::Closure& shutdown_callback);
  ~LaunchInstance();

  void Launch();

 private:
  // TODO(jpoichet) Re-wire to new native input mechanism when available
  void OnEvent(mojo::EventPtr event, const mojo::Callback<void()>& callback);

  mojo::gfx::composition::Compositor* const compositor_;
  mojo::ui::ViewManager* const view_manager_;

  mojo::InterfaceHandle<mojo::Framebuffer> framebuffer_;
  mojo::FramebufferInfoPtr framebuffer_info_;
  mojo::ui::ViewProviderPtr view_provider_;

  ftl::Closure shutdown_callback_;

  std::unique_ptr<LauncherViewTree> view_tree_;

  mojo::ui::ViewOwnerPtr client_view_owner_;

  FTL_DISALLOW_COPY_AND_ASSIGN(LaunchInstance);
};

}  // namespace launcher

#endif  // SERVICES_UI_LAUNCHER_LAUNCH_INSTANCE_H_
