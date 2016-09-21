// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/ui/launcher/launch_instance.h"

#include "base/bind.h"
#include "base/logging.h"
#include "base/trace_event/trace_event.h"
#include "services/ui/launcher/launcher_view_tree.h"

namespace launcher {

LaunchInstance::LaunchInstance(mojo::NativeViewportPtr viewport,
                               mojo::ui::ViewProviderPtr view_provider,
                               mojo::gfx::composition::Compositor* compositor,
                               mojo::ui::ViewManager* view_manager,
                               const base::Closure& shutdown_callback)
    : viewport_(viewport.Pass()),
      view_provider_(view_provider.Pass()),
      compositor_(compositor),
      view_manager_(view_manager),
      shutdown_callback_(shutdown_callback),
      viewport_event_dispatcher_binding_(this) {}

LaunchInstance::~LaunchInstance() {}

void LaunchInstance::Launch() {
  TRACE_EVENT0("launcher", __func__);

  InitViewport();

  view_provider_->CreateView(GetProxy(&client_view_owner_), nullptr);
  view_provider_.reset();
}

void LaunchInstance::InitViewport() {
  viewport_.set_connection_error_handler(base::Bind(
      &LaunchInstance::OnViewportConnectionError, base::Unretained(this)));

  mojo::NativeViewportEventDispatcherPtr dispatcher;
  viewport_event_dispatcher_binding_.Bind(GetProxy(&dispatcher));
  viewport_->SetEventDispatcher(dispatcher.Pass());

  // Match the Nexus 5 aspect ratio initially.
  auto size = mojo::Size::New();
  size->width = 320;
  size->height = 640;

  auto requested_configuration = mojo::SurfaceConfiguration::New();
  viewport_->Create(
      size.Pass(), requested_configuration.Pass(),
      base::Bind(&LaunchInstance::OnViewportCreated, base::Unretained(this)));
}

void LaunchInstance::OnViewportConnectionError() {
  LOG(ERROR) << "Exiting due to viewport connection error.";
  shutdown_callback_.Run();
}

void LaunchInstance::OnViewportCreated(mojo::ViewportMetricsPtr metrics) {
  viewport_->Show();

  mojo::ContextProviderPtr context_provider;
  viewport_->GetContextProvider(GetProxy(&context_provider));

  view_tree_.reset(new LauncherViewTree(compositor_, view_manager_,
                                        context_provider.Pass(), metrics.Pass(),
                                        shutdown_callback_));
  view_tree_->SetRoot(client_view_owner_.Pass());

  RequestUpdatedViewportMetrics();
}

void LaunchInstance::OnViewportMetricsChanged(
    mojo::ViewportMetricsPtr metrics) {
  if (view_tree_) {
    view_tree_->SetViewportMetrics(metrics.Pass());
    RequestUpdatedViewportMetrics();
  }
}

void LaunchInstance::RequestUpdatedViewportMetrics() {
  viewport_->RequestMetrics(base::Bind(
      &LaunchInstance::OnViewportMetricsChanged, base::Unretained(this)));
}

void LaunchInstance::OnEvent(mojo::EventPtr event,
                             const mojo::Callback<void()>& callback) {
  if (view_tree_)
    view_tree_->DispatchEvent(event.Pass());
  callback.Run();
}

}  // namespace launcher
