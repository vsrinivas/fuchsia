// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/root_presenter/presentation.h"

#include "apps/modular/lib/app/connect.h"
#include "apps/mozart/services/views/cpp/formatting.h"
#include "lib/ftl/logging.h"

namespace root_presenter {

Presentation::Presentation(mozart::Compositor* compositor,
                           mozart::ViewManager* view_manager,
                           mozart::ViewOwnerPtr view_owner)
    : compositor_(compositor),
      view_manager_(view_manager),
      view_owner_(std::move(view_owner)),
      input_reader_(&input_interpreter_),
      view_tree_listener_binding_(this),
      view_container_listener_binding_(this) {
  FTL_DCHECK(compositor_);
  FTL_DCHECK(view_manager_);
  FTL_DCHECK(view_owner_);
}

Presentation::~Presentation() {}

void Presentation::Present(ftl::Closure shutdown_callback) {
  shutdown_callback_ = std::move(shutdown_callback);

  compositor_->CreateRenderer(GetProxy(&renderer_), "Presentation");
  renderer_.set_connection_error_handler([this] {
    FTL_LOG(ERROR) << "Renderer died unexpectedly.";
    Shutdown();
  });

  renderer_->GetDisplayInfo([this](mozart::DisplayInfoPtr display_info) {
    display_info_ = std::move(display_info);
    StartInput();
    CreateViewTree();
  });
}

void Presentation::StartInput() {
  input_interpreter_.RegisterDisplay(*display_info_->size);
  input_interpreter_.RegisterCallback([this](mozart::EventPtr event) {
    if (input_dispatcher_)
      input_dispatcher_->DispatchEvent(std::move(event));
  });
  input_reader_.Start();
}

void Presentation::CreateViewTree() {
  FTL_DCHECK(renderer_);
  FTL_DCHECK(view_owner_);
  FTL_DCHECK(display_info_);

  // Register the view tree.
  mozart::ViewTreeListenerPtr view_tree_listener;
  view_tree_listener_binding_.Bind(fidl::GetProxy(&view_tree_listener));
  view_manager_->CreateViewTree(fidl::GetProxy(&view_tree_),
                                std::move(view_tree_listener), "Presentation");
  view_tree_.set_connection_error_handler([this] {
    FTL_LOG(ERROR) << "View tree connection error.";
    Shutdown();
  });

  // Prepare the view container for the root.
  view_tree_->GetContainer(fidl::GetProxy(&view_container_));
  view_container_.set_connection_error_handler([this] {
    FTL_LOG(ERROR) << "View container connection error.";
    Shutdown();
  });
  mozart::ViewContainerListenerPtr view_container_listener;
  view_container_listener_binding_.Bind(
      fidl::GetProxy(&view_container_listener));
  view_container_->SetListener(std::move(view_container_listener));

  // Get view tree services.
  modular::ServiceProviderPtr view_tree_service_provider;
  view_tree_->GetServiceProvider(fidl::GetProxy(&view_tree_service_provider));
  input_dispatcher_ = modular::ConnectToService<mozart::InputDispatcher>(
      view_tree_service_provider.get());
  input_dispatcher_.set_connection_error_handler([this] {
    // This isn't considered a fatal error right now since it is still useful
    // to be able to test a view system that has graphics but no input.
    FTL_LOG(WARNING)
        << "Input dispatcher connection error, input will not work.";
    input_dispatcher_.reset();
  });

  // Attach the renderer.
  view_tree_->SetRenderer(std::move(renderer_));
  view_container_->AddChild(++root_key_, std::move(view_owner_));

  UpdateViewProperties();
}

void Presentation::UpdateViewProperties() {
  auto properties = mozart::ViewProperties::New();
  properties->display_metrics = mozart::DisplayMetrics::New();
  properties->display_metrics->device_pixel_ratio =
      display_info_->device_pixel_ratio;
  properties->view_layout = mozart::ViewLayout::New();
  properties->view_layout->size = display_info_->size.Clone();

  view_container_->SetChildProperties(root_key_, mozart::kSceneVersionNone,
                                      std::move(properties));
}

void Presentation::OnChildAttached(uint32_t child_key,
                                   mozart::ViewInfoPtr child_view_info,
                                   const OnChildAttachedCallback& callback) {
  FTL_DCHECK(child_view_info);

  if (root_key_ == child_key) {
    FTL_VLOG(1) << "OnChildAttached: child_view_info=" << child_view_info;
    root_view_info_ = std::move(child_view_info);
  }
  callback();
}

void Presentation::OnChildUnavailable(
    uint32_t child_key,
    const OnChildUnavailableCallback& callback) {
  if (root_key_ == child_key) {
    FTL_LOG(ERROR) << "Root view terminated unexpectedly.";
    Shutdown();
  }
  callback();
}

void Presentation::OnRendererDied(const OnRendererDiedCallback& callback) {
  FTL_LOG(ERROR) << "Renderer died unexpectedly.";
  Shutdown();
  callback();
}

void Presentation::Shutdown() {
  shutdown_callback_();
}

}  // namespace root_presenter
