// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/launcher/presenter.h"

#include "apps/mozart/glue/base/trace_event.h"
#include "apps/mozart/src/launcher/launcher_view_tree.h"
#include "lib/ftl/logging.h"

namespace launcher {

Presenter::Presenter(mozart::Compositor* compositor,
                     mozart::ViewManager* view_manager,
                     mozart::ViewOwnerPtr view_owner)
    : compositor_(compositor),
      view_manager_(view_manager),
      view_owner_(std::move(view_owner)),
      input_reader_(&input_interpreter_) {
  FTL_DCHECK(compositor_);
  FTL_DCHECK(view_manager_);
  FTL_DCHECK(view_owner_);
}

Presenter::~Presenter() {}

void Presenter::Show() {
  compositor_->CreateRenderer(GetProxy(&renderer_), "Launcher");

  renderer_.set_connection_error_handler([this] {
    FTL_LOG(ERROR) << "Renderer died unexpectedly.";
    shutdown_callback_();
  });

  renderer_->GetDisplayInfo([this](mozart::DisplayInfoPtr display_info) {
    input_interpreter_.RegisterDisplay(*display_info->size);

    view_tree_.reset(new LauncherViewTree(
        view_manager_, std::move(renderer_), std::move(display_info),
        std::move(view_owner_), shutdown_callback_));
    input_interpreter_.RegisterCallback([this](mozart::EventPtr event) {
      TRACE_EVENT0("input", "OnInputEvent");
      view_tree_->DispatchEvent(std::move(event));
    });
    input_reader_.Start();
  });
}

}  // namespace launcher
