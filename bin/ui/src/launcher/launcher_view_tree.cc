// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/launcher/launcher_view_tree.h"

#include "apps/mozart/glue/base/logging.h"
#include "apps/mozart/services/composition/cpp/formatting.h"
#include "apps/mozart/services/views/cpp/formatting.h"
#include "lib/ftl/functional/closure.h"
#include "lib/ftl/logging.h"
#include "mojo/public/cpp/application/connect.h"

namespace launcher {

LauncherViewTree::LauncherViewTree(
    mozart::Compositor* compositor,
    mozart::ViewManager* view_manager,
    mojo::InterfaceHandle<mojo::Framebuffer> framebuffer,
    mojo::FramebufferInfoPtr framebuffer_info,
    mozart::ViewOwnerPtr root_view,
    const ftl::Closure& shutdown_callback)
    : compositor_(compositor),
      view_manager_(view_manager),
      framebuffer_size_(*framebuffer_info->size),
      shutdown_callback_(shutdown_callback),
      view_tree_listener_binding_(this),
      view_container_listener_binding_(this) {
  FTL_DCHECK(compositor_);
  FTL_DCHECK(view_manager_);
  FTL_DCHECK(framebuffer);
  FTL_DCHECK(framebuffer_info);

  // Register the view tree.
  mozart::ViewTreeListenerPtr view_tree_listener;
  view_tree_listener_binding_.Bind(mojo::GetProxy(&view_tree_listener));
  view_manager_->CreateViewTree(mojo::GetProxy(&view_tree_),
                                std::move(view_tree_listener), "LauncherTree");
  view_tree_.set_connection_error_handler(
      [this] { OnViewTreeConnectionError(); });

  // Prepare the view container for the root.
  view_tree_->GetContainer(mojo::GetProxy(&view_container_));
  view_container_.set_connection_error_handler(
      [this] { OnViewTreeConnectionError(); });
  mozart::ViewContainerListenerPtr view_container_listener;
  view_container_listener_binding_.Bind(
      mojo::GetProxy(&view_container_listener));
  view_container_->SetListener(std::move(view_container_listener));

  // Get view tree services.
  mojo::ServiceProviderPtr view_tree_service_provider;
  view_tree_->GetServiceProvider(mojo::GetProxy(&view_tree_service_provider));
  mojo::ConnectToService<mozart::InputDispatcher>(
      view_tree_service_provider.get(), mojo::GetProxy(&input_dispatcher_));
  input_dispatcher_.set_connection_error_handler(
      [this] { OnInputDispatcherConnectionError(); });

  // Attach the renderer.
  mozart::RendererPtr renderer;
  compositor_->CreateRenderer(std::move(framebuffer),
                              std::move(framebuffer_info), GetProxy(&renderer),
                              "Launcher");
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
    DVLOG(1) << "OnChildAttached: child_view_info=" << child_view_info;
    root_view_info_ = std::move(child_view_info);
  }
  callback.Run();
}

void LauncherViewTree::OnChildUnavailable(
    uint32_t child_key,
    const OnChildUnavailableCallback& callback) {
  if (root_key_ == child_key) {
    FTL_LOG(ERROR) << "Root view terminated unexpectedly.";
    Shutdown();
  }
  callback.Run();
}

void LauncherViewTree::OnRendererDied(const OnRendererDiedCallback& callback) {
  FTL_LOG(ERROR) << "Renderer died unexpectedly.";
  Shutdown();
  callback.Run();
}

void LauncherViewTree::UpdateViewProperties() {
  if (!root_was_set_)
    return;

  auto properties = mozart::ViewProperties::New();
  properties->display_metrics = mozart::DisplayMetrics::New();
  // TODO(mikejurka): Create a way to get pixel ratio from framebuffer
  properties->display_metrics->device_pixel_ratio = 1.0;
  properties->view_layout = mozart::ViewLayout::New();
  properties->view_layout->size = framebuffer_size_.Clone();

  view_container_->SetChildProperties(root_key_, mozart::kSceneVersionNone,
                                      std::move(properties));
}

void LauncherViewTree::Shutdown() {
  shutdown_callback_();
}

}  // namespace launcher
