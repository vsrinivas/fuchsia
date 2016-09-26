// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/launcher/launch_instance.h"

#include "apps/mozart/glue/base/trace_event.h"
#include "apps/mozart/src/launcher/launcher_view_tree.h"
#include "lib/ftl/functional/closure.h"
#include "lib/ftl/logging.h"

namespace launcher {

LaunchInstance::LaunchInstance(
    mojo::InterfaceHandle<mojo::Framebuffer> framebuffer,
    mojo::FramebufferInfoPtr framebuffer_info,
    mojo::ui::ViewProviderPtr view_provider,
    mojo::gfx::composition::Compositor* compositor,
    mojo::ui::ViewManager* view_manager,
    const ftl::Closure& shutdown_callback)
    : framebuffer_(std::move(framebuffer)),
      framebuffer_info_(std::move(framebuffer_info)),
      view_provider_(std::move(view_provider)),
      compositor_(compositor),
      view_manager_(view_manager),
      shutdown_callback_(shutdown_callback) {}

LaunchInstance::~LaunchInstance() {}

void LaunchInstance::Launch() {
  TRACE_EVENT0("launcher", __func__);

  InitViewTree();

  view_provider_->CreateView(GetProxy(&client_view_owner_), nullptr);
  view_provider_.reset();
}

void LaunchInstance::InitViewTree() {
  view_tree_.reset(
      new LauncherViewTree(compositor_, view_manager_, std::move(framebuffer_),
                           std::move(framebuffer_info_), shutdown_callback_));
  view_tree_->SetRoot(std::move(client_view_owner_));
}

void LaunchInstance::OnEvent(mojo::EventPtr event,
                             const mojo::Callback<void()>& callback) {
  if (view_tree_)
    view_tree_->DispatchEvent(std::move(event));
  callback.Run();
}

}  // namespace launcher
