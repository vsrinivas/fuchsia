// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/launcher/launch_instance.h"

#include "apps/mozart/glue/base/trace_event.h"
#include "apps/mozart/src/launcher/launcher_view_tree.h"
#include "lib/ftl/functional/closure.h"
#include "lib/ftl/logging.h"
#include "lib/mtl/tasks/message_loop.h"

namespace launcher {

constexpr uint16_t kInputPollIntervalMilliseconds = 20;

LaunchInstance::LaunchInstance(
    mojo::gfx::composition::Compositor* compositor,
    mojo::ui::ViewManager* view_manager,
    mojo::InterfaceHandle<mojo::Framebuffer> framebuffer,
    mojo::FramebufferInfoPtr framebuffer_info,
    mojo::ui::ViewProviderPtr view_provider,
    const ftl::Closure& shutdown_callback)
    : compositor_(compositor),
      view_manager_(view_manager),
      framebuffer_(std::move(framebuffer)),
      framebuffer_info_(std::move(framebuffer_info)),
      framebuffer_size_(*framebuffer_info_->size),
      view_provider_(std::move(view_provider)),
      shutdown_callback_(shutdown_callback) {
  FTL_DCHECK(compositor_);
  FTL_DCHECK(view_manager_);
  FTL_DCHECK(framebuffer_);
  FTL_DCHECK(framebuffer_info_);
  FTL_DCHECK(view_provider_);
}

LaunchInstance::~LaunchInstance() {}

void LaunchInstance::Launch() {
  TRACE_EVENT0("launcher", __func__);

  view_provider_->CreateView(GetProxy(&client_view_owner_), nullptr);
  view_provider_.reset();

  view_tree_.reset(
      new LauncherViewTree(compositor_, view_manager_, std::move(framebuffer_),
                           std::move(framebuffer_info_), shutdown_callback_));
  view_tree_->SetRoot(std::move(client_view_owner_));

  input_device_monitor_.Start();
  mtl::MessageLoop::GetCurrent()->task_runner()->PostDelayedTask(
      [this] { CheckInput(); },
      ftl::TimeDelta::FromMilliseconds(kInputPollIntervalMilliseconds));
}

void LaunchInstance::CheckInput() {
  input_device_monitor_.CheckInput(
      [this](mojo::EventPtr event) {
        view_tree_->DispatchEvent(std::move(event));
      },
      framebuffer_size_);

  mtl::MessageLoop::GetCurrent()->task_runner()->PostDelayedTask(
      [this] { CheckInput(); },
      ftl::TimeDelta::FromMilliseconds(kInputPollIntervalMilliseconds));
}

}  // namespace launcher
