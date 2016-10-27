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
    mozart::Compositor* compositor,
    mozart::ViewManager* view_manager,
    mojo::InterfaceHandle<mojo::Framebuffer> framebuffer,
    mojo::FramebufferInfoPtr framebuffer_info,
    mozart::ViewOwnerPtr view_owner,
    const ftl::Closure& shutdown_callback)
    : compositor_(compositor),
      view_manager_(view_manager),
      framebuffer_(std::move(framebuffer)),
      framebuffer_info_(std::move(framebuffer_info)),
      framebuffer_size_(*framebuffer_info_->size),
      root_view_owner_(std::move(view_owner)),
      shutdown_callback_(shutdown_callback),
      input_reader_(&input_interpreter_) {
  FTL_DCHECK(compositor_);
  FTL_DCHECK(view_manager_);
  FTL_DCHECK(framebuffer_);
  FTL_DCHECK(framebuffer_info_);
}

LaunchInstance::~LaunchInstance() {}

void LaunchInstance::Launch() {
  TRACE_EVENT0("launcher", __func__);

  view_tree_.reset(
      new LauncherViewTree(compositor_, view_manager_, std::move(framebuffer_),
                           std::move(framebuffer_info_),
                           std::move(root_view_owner_), shutdown_callback_));
  input_interpreter_.RegisterDisplay(framebuffer_size_);
  input_interpreter_.RegisterCallback([this](mozart::EventPtr event) {
    TRACE_EVENT0("input", "OnInputEvent");
    view_tree_->DispatchEvent(std::move(event));
  });
  input_reader_.Start();
}

}  // namespace launcher
