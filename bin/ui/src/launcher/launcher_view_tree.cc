// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/ui/launcher/launcher_view_tree.h"

#include "base/bind.h"
#include "mojo/public/cpp/application/connect.h"
#include "apps/mozart/services/composition/cpp/formatting.h"
#include "mojo/services/ui/views/cpp/formatting.h"

namespace launcher {

LauncherViewTree::LauncherViewTree(
    mojo::gfx::composition::Compositor* compositor,
    mojo::ui::ViewManager* view_manager,
    mojo::ContextProviderPtr context_provider,
    mojo::ViewportMetricsPtr viewport_metrics,
    const base::Closure& shutdown_callback)
    : compositor_(compositor),
      view_manager_(view_manager),
      context_provider_(context_provider.Pass()),
      viewport_metrics_(viewport_metrics.Pass()),
      shutdown_callback_(shutdown_callback),
      view_tree_listener_binding_(this),
      view_container_listener_binding_(this) {
  DCHECK(viewport_metrics_);

  // Register the view tree.
  mojo::ui::ViewTreeListenerPtr view_tree_listener;
  view_tree_listener_binding_.Bind(mojo::GetProxy(&view_tree_listener));
  view_manager_->CreateViewTree(mojo::GetProxy(&view_tree_),
                                view_tree_listener.Pass(), "LauncherTree");
  view_tree_.set_connection_error_handler(base::Bind(
      &LauncherViewTree::OnViewTreeConnectionError, base::Unretained(this)));

  // Prepare the view container for the root.
  view_tree_->GetContainer(mojo::GetProxy(&view_container_));
  view_container_.set_connection_error_handler(base::Bind(
      &LauncherViewTree::OnViewTreeConnectionError, base::Unretained(this)));
  mojo::ui::ViewContainerListenerPtr view_container_listener;
  view_container_listener_binding_.Bind(
      mojo::GetProxy(&view_container_listener));
  view_container_->SetListener(view_container_listener.Pass());

  // Get view tree services.
  mojo::ServiceProviderPtr view_tree_service_provider;
  view_tree_->GetServiceProvider(mojo::GetProxy(&view_tree_service_provider));
  mojo::ConnectToService<mojo::ui::InputDispatcher>(
      view_tree_service_provider.get(), mojo::GetProxy(&input_dispatcher_));
  input_dispatcher_.set_connection_error_handler(
      base::Bind(&LauncherViewTree::OnInputDispatcherConnectionError,
                 base::Unretained(this)));

  // Attach the renderer.
  mojo::gfx::composition::RendererPtr renderer;
  compositor_->CreateRenderer(context_provider_.Pass(), GetProxy(&renderer),
                              "Launcher");
  view_tree_->SetRenderer(renderer.Pass());
}

LauncherViewTree::~LauncherViewTree() {}

void LauncherViewTree::SetRoot(mojo::ui::ViewOwnerPtr owner) {
  if (root_was_set_) {
    root_was_set_ = false;
    view_container_->RemoveChild(root_key_, nullptr);
  }
  if (owner) {
    root_was_set_ = true;
    view_container_->AddChild(++root_key_, owner.Pass());
    UpdateViewProperties();
  }
}

void LauncherViewTree::SetViewportMetrics(
    mojo::ViewportMetricsPtr viewport_metrics) {
  DCHECK(viewport_metrics);

  viewport_metrics_ = viewport_metrics.Pass();
  UpdateViewProperties();
}

void LauncherViewTree::DispatchEvent(mojo::EventPtr event) {
  if (input_dispatcher_)
    input_dispatcher_->DispatchEvent(event.Pass());
}

void LauncherViewTree::OnViewTreeConnectionError() {
  LOG(ERROR) << "View tree connection error.";
  Shutdown();
}

void LauncherViewTree::OnInputDispatcherConnectionError() {
  // This isn't considered a fatal error right now since it is still useful
  // to be able to test a view system that has graphics but no input.
  LOG(WARNING) << "Input dispatcher connection error, input will not work.";
  input_dispatcher_.reset();
}

void LauncherViewTree::OnChildAttached(
    uint32_t child_key,
    mojo::ui::ViewInfoPtr child_view_info,
    const OnChildAttachedCallback& callback) {
  DCHECK(child_view_info);

  if (root_key_ == child_key) {
    DVLOG(1) << "OnChildAttached: child_view_info=" << child_view_info;
    root_view_info_ = child_view_info.Pass();
  }
  callback.Run();
}

void LauncherViewTree::OnChildUnavailable(
    uint32_t child_key,
    const OnChildUnavailableCallback& callback) {
  if (root_key_ == child_key) {
    LOG(ERROR) << "Root view terminated unexpectedly.";
    Shutdown();
  }
  callback.Run();
}

void LauncherViewTree::OnRendererDied(const OnRendererDiedCallback& callback) {
  LOG(ERROR) << "Renderer died unexpectedly.";
  Shutdown();
  callback.Run();
}

void LauncherViewTree::UpdateViewProperties() {
  if (!root_was_set_)
    return;

  auto properties = mojo::ui::ViewProperties::New();
  properties->display_metrics = mojo::ui::DisplayMetrics::New();
  properties->display_metrics->device_pixel_ratio =
      viewport_metrics_->device_pixel_ratio;
  properties->view_layout = mojo::ui::ViewLayout::New();
  properties->view_layout->size = viewport_metrics_->size.Clone();

  view_container_->SetChildProperties(
      root_key_, mojo::gfx::composition::kSceneVersionNone, properties.Pass());
}

void LauncherViewTree::Shutdown() {
  shutdown_callback_.Run();
}

}  // namespace launcher
