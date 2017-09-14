// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ui/view_framework/base_view.h"

#include <trace/event.h>

#include "lib/app/cpp/connect.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/time/time_point.h"

namespace mozart {
namespace {

scenic::SceneManagerPtr GetSceneManager(ViewManager* view_manager) {
  scenic::SceneManagerPtr scene_manager;
  view_manager->GetSceneManager(scene_manager.NewRequest());
  return scene_manager;
}

}  // namespace

BaseView::BaseView(ViewManagerPtr view_manager,
                   fidl::InterfaceRequest<ViewOwner> view_owner_request,
                   const std::string& label)
    : view_manager_(std::move(view_manager)),
      view_listener_binding_(this),
      view_container_listener_binding_(this),
      input_listener_binding_(this),
      session_(GetSceneManager(view_manager_.get()).get()),
      parent_node_(&session_) {
  FXL_DCHECK(view_manager_);
  FXL_DCHECK(view_owner_request);

  zx::eventpair parent_export_token;
  parent_node_.BindAsRequest(&parent_export_token);
  view_manager_->CreateView(view_.NewRequest(), std::move(view_owner_request),
                            view_listener_binding_.NewBinding(),
                            std::move(parent_export_token), label);

  app::ConnectToService(GetViewServiceProvider(),
                        input_connection_.NewRequest());
  input_connection_->SetEventListener(input_listener_binding_.NewBinding());

  session_.set_event_handler(
      std::bind(&BaseView::HandleSessionEvents, this, std::placeholders::_1));
  parent_node_.SetEventMask(scenic::kMetricsEventMask);
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

void BaseView::SetReleaseHandler(fxl::Closure callback) {
  view_listener_binding_.set_connection_error_handler(callback);
}

void BaseView::InvalidateScene() {
  if (invalidate_pending_)
    return;

  invalidate_pending_ = true;
  if (!present_pending_)
    PresentScene(zx_time_get(ZX_CLOCK_MONOTONIC));
}

void BaseView::PresentScene(zx_time_t presentation_time) {
  FXL_DCHECK(!present_pending_);

  present_pending_ = true;
  session()->Present(
      presentation_time, [this](scenic::PresentationInfoPtr info) {
        FXL_DCHECK(present_pending_);

        zx_time_t next_presentation_time =
            info->presentation_time + info->presentation_interval;

        bool present_needed = false;
        if (invalidate_pending_) {
          invalidate_pending_ = false;
          OnSceneInvalidated(std::move(info));
          present_needed = true;
        }

        present_pending_ = false;
        if (present_needed)
          PresentScene(next_presentation_time);
      });
}

void BaseView::HandleSessionEvents(fidl::Array<scenic::EventPtr> events) {
  scenic::Metrics* new_metrics = nullptr;
  for (const auto& event : events) {
    if (event->is_metrics() &&
        event->get_metrics()->node_id == parent_node_.id()) {
      new_metrics = event->get_metrics()->metrics.get();
    }
  }

  if (new_metrics && !original_metrics_.Equals(*new_metrics)) {
    original_metrics_ = *new_metrics;
    AdjustMetricsAndPhysicalSize();
  }

  OnSessionEvent(std::move(events));
}

void BaseView::SetNeedSquareMetrics(bool enable) {
  if (need_square_metrics_ == enable)
    return;
  need_square_metrics_ = true;
  AdjustMetricsAndPhysicalSize();
}

void BaseView::AdjustMetricsAndPhysicalSize() {
  adjusted_metrics_ = original_metrics_;
  if (need_square_metrics_) {
    adjusted_metrics_.scale_x = adjusted_metrics_.scale_y =
        std::max(original_metrics_.scale_x, original_metrics_.scale_y);
  }

  physical_size_.width = logical_size_.width * adjusted_metrics_.scale_x;
  physical_size_.height = logical_size_.height * adjusted_metrics_.scale_y;

  InvalidateScene();
}

void BaseView::OnPropertiesChanged(ViewPropertiesPtr old_properties) {}

void BaseView::OnSceneInvalidated(
    scenic::PresentationInfoPtr presentation_info) {}

void BaseView::OnSessionEvent(fidl::Array<scenic::EventPtr> events) {}

bool BaseView::OnInputEvent(mozart::InputEventPtr event) {
  return false;
}

void BaseView::OnChildAttached(uint32_t child_key,
                               ViewInfoPtr child_view_info) {}

void BaseView::OnChildUnavailable(uint32_t child_key) {}

void BaseView::OnPropertiesChanged(
    ViewPropertiesPtr properties,
    const OnPropertiesChangedCallback& callback) {
  FXL_DCHECK(properties);
  TRACE_DURATION("view", "OnPropertiesChanged");

  ViewPropertiesPtr old_properties = std::move(properties_);
  properties_ = std::move(properties);

  if (!logical_size_.Equals(*properties_->view_layout->size)) {
    logical_size_ = *properties_->view_layout->size;
    AdjustMetricsAndPhysicalSize();
  }

  OnPropertiesChanged(std::move(old_properties));

  callback();
}

void BaseView::OnChildAttached(uint32_t child_key,
                               ViewInfoPtr child_view_info,
                               const OnChildUnavailableCallback& callback) {
  FXL_DCHECK(child_view_info);

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
