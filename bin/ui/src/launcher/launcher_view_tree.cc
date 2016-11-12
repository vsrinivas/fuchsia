// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/launcher/launcher_view_tree.h"

#include "apps/modular/lib/app/connect.h"
#include "apps/mozart/services/views/cpp/formatting.h"
#include "lib/ftl/functional/closure.h"
#include "lib/ftl/logging.h"

namespace launcher {

LauncherViewTree::LauncherViewTree(mozart::ViewManager* view_manager,
                                   mozart::RendererPtr renderer,
                                   mozart::DisplayInfoPtr display_info,
                                   mozart::ViewOwnerPtr root_view,
                                   const ftl::Closure& shutdown_callback)
    : view_manager_(view_manager),
      display_info_(std::move(display_info)),
      shutdown_callback_(shutdown_callback),
      view_tree_listener_binding_(this),
      view_container_listener_binding_(this) {
  FTL_DCHECK(view_manager_);
  FTL_DCHECK(display_info_);
  FTL_DCHECK(renderer);

  // Register the view tree.
  mozart::ViewTreeListenerPtr view_tree_listener;
  view_tree_listener_binding_.Bind(fidl::GetProxy(&view_tree_listener));
  view_manager_->CreateViewTree(fidl::GetProxy(&view_tree_),
                                std::move(view_tree_listener), "LauncherTree");
  view_tree_.set_connection_error_handler(
      [this] { OnViewTreeConnectionError(); });

  // Prepare the view container for the root.
  view_tree_->GetContainer(fidl::GetProxy(&view_container_));
  view_container_.set_connection_error_handler(
      [this] { OnViewTreeConnectionError(); });
  mozart::ViewContainerListenerPtr view_container_listener;
  view_container_listener_binding_.Bind(
      fidl::GetProxy(&view_container_listener));
  view_container_->SetListener(std::move(view_container_listener));

  // Get view tree services.
  modular::ServiceProviderPtr view_tree_service_provider;
  view_tree_->GetServiceProvider(fidl::GetProxy(&view_tree_service_provider));
  input_dispatcher_ = modular::ConnectToService<mozart::InputDispatcher>(
      view_tree_service_provider.get());
  input_dispatcher_.set_connection_error_handler(
      [this] { OnInputDispatcherConnectionError(); });

  // Attach the renderer.
  view_tree_->SetRenderer(std::move(renderer));
  if (root_view) {
    root_was_set_ = true;
    view_container_->AddChild(++root_key_, std::move(root_view));
  }

  UpdateViewProperties();
}

LauncherViewTree::~LauncherViewTree() {}

void LauncherViewTree::DispatchEvent(mozart::EventPtr event) {
  if (input_dispatcher_)
    input_dispatcher_->DispatchEvent(std::move(event));
}

void LauncherViewTree::OnViewTreeConnectionError() {
  FTL_LOG(ERROR) << "View tree connection error.";
  Shutdown();
}

void LauncherViewTree::OnInputDispatcherConnectionError() {
  // This isn't considered a fatal error right now since it is still useful
  // to be able to test a view system that has graphics but no input.
  FTL_LOG(WARNING) << "Input dispatcher connection error, input will not work.";
  input_dispatcher_.reset();
}

void LauncherViewTree::OnChildAttached(
    uint32_t child_key,
    mozart::ViewInfoPtr child_view_info,
    const OnChildAttachedCallback& callback) {
  FTL_DCHECK(child_view_info);

  if (root_key_ == child_key) {
    FTL_VLOG(1) << "OnChildAttached: child_view_info=" << child_view_info;
    root_view_info_ = std::move(child_view_info);
  }
  callback();
}

void LauncherViewTree::OnChildUnavailable(
    uint32_t child_key,
    const OnChildUnavailableCallback& callback) {
  if (root_key_ == child_key) {
    FTL_LOG(ERROR) << "Root view terminated unexpectedly.";
    Shutdown();
  }
  callback();
}

void LauncherViewTree::OnRendererDied(const OnRendererDiedCallback& callback) {
  FTL_LOG(ERROR) << "Renderer died unexpectedly.";
  Shutdown();
  callback();
}

void LauncherViewTree::UpdateViewProperties() {
  if (!root_was_set_)
    return;

  auto properties = mozart::ViewProperties::New();
  properties->display_metrics = mozart::DisplayMetrics::New();
  properties->display_metrics->device_pixel_ratio =
      display_info_->device_pixel_ratio;
  properties->view_layout = mozart::ViewLayout::New();
  properties->view_layout->size = display_info_->size.Clone();

  view_container_->SetChildProperties(root_key_, mozart::kSceneVersionNone,
                                      std::move(properties));
}

void LauncherViewTree::Shutdown() {
  shutdown_callback_();
}

}  // namespace launcher
