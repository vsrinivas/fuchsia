// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/launcher/launch_instance.h"

#include "apps/mozart/glue/base/trace_event.h"
#include "apps/mozart/src/launcher/launcher_view_tree.h"
#include "lib/ftl/functional/closure.h"
#include "lib/ftl/logging.h"
#include "mojo/public/cpp/application/connect.h"

namespace launcher {

LaunchInstance::LaunchInstance(mozart::Compositor* compositor,
                               mozart::ViewManager* view_manager,
                               mozart::ViewOwnerPtr view_owner,
                               const ftl::Closure& shutdown_callback)
    : compositor_(compositor),
      view_manager_(view_manager),
      root_view_owner_(std::move(view_owner)),
      shutdown_callback_(shutdown_callback),
      input_reader_(&input_interpreter_) {
  FTL_DCHECK(compositor_);
  FTL_DCHECK(view_manager_);
}

LaunchInstance::~LaunchInstance() {}

void LaunchInstance::Launch() {
  TRACE_EVENT0("launcher", __func__);

  compositor_->CreateRenderer(GetProxy(&renderer_), "Launcher");
  renderer_->GetDisplayInfo([this](mozart::DisplayInfoPtr display_info) {
    input_interpreter_.RegisterDisplay(*display_info->size);

    view_tree_.reset(new LauncherViewTree(
        view_manager_, std::move(renderer_), std::move(display_info),
        std::move(root_view_owner_), shutdown_callback_));
    input_interpreter_.RegisterCallback([this](mozart::EventPtr event) {
      TRACE_EVENT0("input", "OnInputEvent");
      view_tree_->DispatchEvent(std::move(event));
    });
    input_reader_.Start();
  });
}

}  // namespace launcher
