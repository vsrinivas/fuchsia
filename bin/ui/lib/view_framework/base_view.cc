// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/lib/view_framework/base_view.h"

#include "apps/tracing/lib/trace/event.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/time/time_point.h"

namespace mozart {

BaseView::BaseView(ViewManagerPtr view_manager,
                   fidl::InterfaceRequest<ViewOwner> view_owner_request,
                   const std::string& label)
    : view_manager_(std::move(view_manager)),
      view_listener_binding_(this),
      view_container_listener_binding_(this) {
  FTL_DCHECK(view_manager_);
  FTL_DCHECK(view_owner_request);

  ViewListenerPtr view_listener;
  view_listener_binding_.Bind(view_listener.NewRequest());
  view_manager_->CreateView(view_.NewRequest(), std::move(view_owner_request),
                            std::move(view_listener), label);
  view_->CreateScene(scene_.NewRequest());
}

BaseView::~BaseView() {}

modular::ServiceProvider* BaseView::GetViewServiceProvider() {
  if (!view_service_provider_)
    view_->GetServiceProvider(view_service_provider_.NewRequest());
  return view_service_provider_.get();
}

ViewContainer* BaseView::GetViewContainer() {
  if (!view_container_) {
    view_->GetContainer(view_container_.NewRequest());
    ViewContainerListenerPtr view_container_listener;
    view_container_listener_binding_.Bind(view_container_listener.NewRequest());
    view_container_->SetListener(std::move(view_container_listener));
  }
  return view_container_.get();
}

void BaseView::SetReleaseHandler(ftl::Closure callback) {
  view_listener_binding_.set_connection_error_handler(callback);
}

SceneMetadataPtr BaseView::CreateSceneMetadata() const {
  auto metadata = SceneMetadata::New();
  metadata->version = scene_version_;
  metadata->presentation_time = frame_tracker_.frame_info().presentation_time;
  return metadata;
}

void BaseView::Invalidate() {
  if (!invalidated_) {
    invalidated_ = true;
    view_->Invalidate();
  }
}

void BaseView::OnPropertiesChanged(ViewPropertiesPtr old_properties) {}

void BaseView::OnLayout() {}

void BaseView::OnDraw() {}

void BaseView::OnChildAttached(uint32_t child_key,
                               ViewInfoPtr child_view_info) {}

void BaseView::OnChildUnavailable(uint32_t child_key) {}

void BaseView::OnInvalidation(ViewInvalidationPtr invalidation,
                              const OnInvalidationCallback& callback) {
  FTL_DCHECK(invalidation);
  FTL_DCHECK(invalidation->frame_info);
  TRACE_DURATION("view", "OnInvalidation");

  invalidated_ = false;
  frame_tracker_.Update(*invalidation->frame_info, ftl::TimePoint::Now());
  scene_version_ = invalidation->scene_version;

  if (invalidation->properties) {
    FTL_DCHECK(invalidation->properties->display_metrics);
    FTL_DCHECK(invalidation->properties->view_layout);
    FTL_DCHECK(invalidation->properties->view_layout->size);
    TRACE_DURATION("view", "OnPropertiesChanged");

    ViewPropertiesPtr old_properties = std::move(properties_);
    properties_ = std::move(invalidation->properties);
    OnPropertiesChanged(std::move(old_properties));
  }

  if (!properties_)
    return;

  {
    TRACE_DURATION("view", "OnLayout");
    OnLayout();
  }

  if (invalidation->container_flush_token) {
    FTL_DCHECK(view_container_);  // we must have added children
    view_container_->FlushChildren(invalidation->container_flush_token);
  }

  {
    TRACE_DURATION("view", "OnDraw");
    OnDraw();
  }

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

}  // namespace mozart
