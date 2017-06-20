// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/lib/view_framework/base_view.h"

#include "application/lib/app/connect.h"
#include "apps/tracing/lib/trace/event.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/time/time_point.h"

namespace mozart {
namespace {

mozart2::SessionPtr CreateSession(ViewManager* view_manager) {
  mozart2::SessionPtr session;
  mozart2::SceneManagerPtr scene_manager;
  view_manager->GetSceneManager(scene_manager.NewRequest());
  scene_manager->CreateSession(session.NewRequest(), nullptr);
  return session;
}

}  // namespace

BaseView::BaseView(ViewManagerPtr view_manager,
                   fidl::InterfaceRequest<ViewOwner> view_owner_request,
                   const std::string& label)
    : view_manager_(std::move(view_manager)),
      view_listener_binding_(this),
      view_container_listener_binding_(this),
      input_listener_binding_(this),
      session_(CreateSession(view_manager_.get())),
      parent_node_(&session_) {
  FTL_DCHECK(view_manager_);
  FTL_DCHECK(view_owner_request);

  mx::eventpair parent_export_token;
  parent_node_.BindAsRequest(&parent_export_token);
  view_manager_->CreateView(view_.NewRequest(), std::move(view_owner_request),
                            view_listener_binding_.NewBinding(),
                            std::move(parent_export_token), label);

  app::ConnectToService(GetViewServiceProvider(),
                        input_connection_.NewRequest());
  input_connection_->SetEventListener(input_listener_binding_.NewBinding());
}

BaseView::~BaseView() = default;

app::ServiceProvider* BaseView::GetViewServiceProvider() {
  if (!view_service_provider_)
    view_->GetServiceProvider(view_service_provider_.NewRequest());
  return view_service_provider_.get();
}

ViewContainer* BaseView::GetViewContainer() {
  if (!view_container_) {
    view_->GetContainer(view_container_.NewRequest());
    view_container_->SetListener(view_container_listener_binding_.NewBinding());
  }
  return view_container_.get();
}

void BaseView::SetReleaseHandler(ftl::Closure callback) {
  view_listener_binding_.set_connection_error_handler(callback);
}

void BaseView::InvalidateScene() {
  if (invalidate_pending_)
    return;

  invalidate_pending_ = true;
  if (!present_pending_)
    PresentScene();
}

void BaseView::PresentScene() {
  FTL_DCHECK(!present_pending_);

  present_pending_ = true;
  session()->Present(0, [this](mozart2::PresentationInfoPtr info) {
    FTL_DCHECK(present_pending_);

    bool present_needed = false;
    if (invalidate_pending_) {
      invalidate_pending_ = false;
      OnSceneInvalidated(std::move(info));
      present_needed = true;
    }

    present_pending_ = false;
    if (present_needed)
      PresentScene();
  });
}

void BaseView::OnPropertiesChanged(ViewPropertiesPtr old_properties) {}

void BaseView::OnSceneInvalidated(
    mozart2::PresentationInfoPtr presentation_info) {}

bool BaseView::OnInputEvent(mozart::InputEventPtr event) {
  return false;
}

void BaseView::OnChildAttached(uint32_t child_key,
                               ViewInfoPtr child_view_info) {}

void BaseView::OnChildUnavailable(uint32_t child_key) {}

void BaseView::OnPropertiesChanged(
    ViewPropertiesPtr properties,
    const OnPropertiesChangedCallback& callback) {
  FTL_DCHECK(properties);
  TRACE_DURATION("view", "OnPropertiesChanged");

  ViewPropertiesPtr old_properties = std::move(properties_);
  properties_ = std::move(properties);
  size_ = *properties_->view_layout->size;
  OnPropertiesChanged(std::move(old_properties));

  callback();
}

void BaseView::OnChildAttached(uint32_t child_key,
                               ViewInfoPtr child_view_info,
                               const OnChildUnavailableCallback& callback) {
  FTL_DCHECK(child_view_info);

  TRACE_DURATION("view", "OnChildAttached", "child_key", child_key);
  OnChildAttached(child_key, std::move(child_view_info));
  callback();
}

void BaseView::OnChildUnavailable(uint32_t child_key,
                                  const OnChildUnavailableCallback& callback) {
  TRACE_DURATION("view", "OnChildUnavailable", "child_key", child_key);
  OnChildUnavailable(child_key);
  callback();
}

void BaseView::OnEvent(mozart::InputEventPtr event,
                       const OnEventCallback& callback) {
  TRACE_DURATION("view", "OnEvent");
  bool handled = OnInputEvent(std::move(event));
  callback(handled);
}

}  // namespace mozart
