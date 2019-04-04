// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/public/lib/ui/base_view/cpp/v1_base_view.h"

#include <lib/component/cpp/connect.h>
#include <src/lib/fxl/logging.h>
#include <src/lib/fxl/time/time_point.h>
#include <lib/ui/geometry/cpp/geometry_util.h>
#include <lib/zx/eventpair.h>
#include <trace/event.h>

namespace scenic {

V1BaseView::V1BaseView(scenic::ViewContext context,
                       const std::string& debug_name)
    : startup_context_(context.startup_context),
      view_manager_(startup_context_->ConnectToEnvironmentService<
                    ::fuchsia::ui::viewsv1::ViewManager>()),
      view_listener_binding_(this),
      view_container_listener_binding_(this),
      incoming_services_(context.outgoing_services.Bind()),
      outgoing_services_(std::move(context.incoming_services)),
      session_(std::move(context.session_and_listener_request)),
      parent_node_(&session_) {
  zx::eventpair view_token;
  if (context.view_token2.value) {
    view_token = std::move(context.view_token2.value);
  } else {
    FXL_DCHECK(context.view_token);
    view_token = std::move(context.view_token);
  }

  session_.SetDebugName(debug_name);

  zx::eventpair parent_export_token;
  parent_node_.BindAsRequest(&parent_export_token);

  view_manager_->CreateView2(view_.NewRequest(), std::move(view_token),
                             view_listener_binding_.NewBinding(),
                             std::move(parent_export_token), debug_name);

  session_.set_event_handler(
      std::bind(&V1BaseView::HandleSessionEvents, this, std::placeholders::_1));
  parent_node_.SetEventMask(fuchsia::ui::gfx::kMetricsEventMask);
}

V1BaseView::~V1BaseView() = default;

fuchsia::sys::ServiceProvider* V1BaseView::GetViewServiceProvider() {
  if (!view_service_provider_)
    view_->GetServiceProvider(view_service_provider_.NewRequest());
  return view_service_provider_.get();
}

::fuchsia::ui::viewsv1::ViewContainer* V1BaseView::GetViewContainer() {
  if (!view_container_) {
    view_->GetContainer(view_container_.NewRequest());
    view_container_->SetListener(view_container_listener_binding_.NewBinding());
  }
  return view_container_.get();
}

void V1BaseView::SetReleaseHandler(
    fit::function<void(zx_status_t status)> callback) {
  view_listener_binding_.set_error_handler(std::move(callback));
}

void V1BaseView::InvalidateScene() {
  if (invalidate_pending_)
    return;

  invalidate_pending_ = true;

  // Present the scene ASAP. Pass in the last presentation time; otherwise, if
  // presentation_time argument is less than the previous time passed to
  // PresentScene, the Session will be closed.
  // (We cannot use the current time because the last requested presentation
  // time, |last_presentation_time_|, could still be in the future. This is
  // because Session.Present() returns after it _begins_ preparing the given
  // frame, not after it is presented.)
  if (!present_pending_)
    PresentScene(last_presentation_time_);
}

void V1BaseView::PresentScene(zx_time_t presentation_time) {
  FXL_DCHECK(!present_pending_);

  present_pending_ = true;

  // Keep track of the most recent presentation time we've passed to
  // Session.Present(), for use in InvalidateScene().
  last_presentation_time_ = presentation_time;

  session()->Present(presentation_time,
                     [this](fuchsia::images::PresentationInfo info) {
                       FXL_DCHECK(present_pending_);

                       zx_time_t next_presentation_time =
                           info.presentation_time + info.presentation_interval;

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

void V1BaseView::HandleSessionEvents(
    std::vector<fuchsia::ui::scenic::Event> events) {
  const fuchsia::ui::gfx::Metrics* new_metrics = nullptr;
  for (size_t i = 0; i < events.size(); ++i) {
    const auto& event = events[i];
    if (event.is_gfx()) {
      const fuchsia::ui::gfx::Event& scenic_event = event.gfx();
      if (scenic_event.is_metrics() &&
          scenic_event.metrics().node_id == parent_node_.id()) {
        new_metrics = &scenic_event.metrics().metrics;
      }
    } else if (event.is_input()) {
      // Act on input event just once.
      OnInputEvent(std::move(events[i].input()));
      // Create a dummy event to safely take its place.
      fuchsia::ui::scenic::Command unhandled;
      fuchsia::ui::scenic::Event unhandled_event;
      unhandled_event.set_unhandled(std::move(unhandled));
      events[i] = std::move(unhandled_event);
    }
  }

  if (new_metrics && !fidl::Equals(original_metrics_, *new_metrics)) {
    original_metrics_ = *new_metrics;
    AdjustMetricsAndPhysicalSize();
  }

  OnScenicEvent(std::move(events));
}

void V1BaseView::SetNeedSquareMetrics(bool enable) {
  if (need_square_metrics_ == enable)
    return;
  need_square_metrics_ = true;
  AdjustMetricsAndPhysicalSize();
}

void V1BaseView::AdjustMetricsAndPhysicalSize() {
  adjusted_metrics_ = original_metrics_;
  if (need_square_metrics_) {
    adjusted_metrics_.scale_x = adjusted_metrics_.scale_y =
        std::max(original_metrics_.scale_x, original_metrics_.scale_y);
  }

  physical_size_.width = logical_size_.width * adjusted_metrics_.scale_x;
  physical_size_.height = logical_size_.height * adjusted_metrics_.scale_y;

  InvalidateScene();
}

void V1BaseView::OnPropertiesChanged(
    ::fuchsia::ui::viewsv1::ViewProperties old_properties) {}

void V1BaseView::OnSceneInvalidated(
    fuchsia::images::PresentationInfo presentation_info) {}

void V1BaseView::OnScenicEvent(std::vector<fuchsia::ui::scenic::Event> events) {
}

bool V1BaseView::OnInputEvent(fuchsia::ui::input::InputEvent event) {
  return false;
}

void V1BaseView::OnChildAttached(
    uint32_t child_key, ::fuchsia::ui::viewsv1::ViewInfo child_view_info) {}

void V1BaseView::OnChildUnavailable(uint32_t child_key) {}

void V1BaseView::OnPropertiesChanged(
    ::fuchsia::ui::viewsv1::ViewProperties properties,
    OnPropertiesChangedCallback callback) {
  TRACE_DURATION("view", "OnPropertiesChanged");

  ::fuchsia::ui::viewsv1::ViewProperties old_properties =
      std::move(properties_);
  properties_ = std::move(properties);

  if (!fidl::Equals(logical_size_, properties_.view_layout->size)) {
    logical_size_ = properties_.view_layout->size;
    AdjustMetricsAndPhysicalSize();
  }

  OnPropertiesChanged(std::move(old_properties));

  callback();
}

void V1BaseView::OnChildAttached(
    uint32_t child_key, ::fuchsia::ui::viewsv1::ViewInfo child_view_info,
    OnChildUnavailableCallback callback) {
  TRACE_DURATION("view", "OnChildAttached", "child_key", child_key);
  OnChildAttached(child_key, std::move(child_view_info));
  callback();
}

void V1BaseView::OnChildUnavailable(uint32_t child_key,
                                    OnChildUnavailableCallback callback) {
  TRACE_DURATION("view", "OnChildUnavailable", "child_key", child_key);
  OnChildUnavailable(child_key);
  callback();
}

}  // namespace scenic
