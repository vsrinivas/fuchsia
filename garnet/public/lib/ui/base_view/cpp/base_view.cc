// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ui/base_view/cpp/base_view.h"

#include <trace/event.h>

#include "src/lib/fxl/logging.h"
#include "lib/ui/gfx/cpp/math.h"
#include "lib/ui/scenic/cpp/commands.h"
#include "lib/ui/scenic/cpp/view_token_pair.h"

namespace scenic {

BaseView::BaseView(ViewContext context, const std::string& debug_name)
    : startup_context_(context.startup_context),
      incoming_services_(context.outgoing_services.Bind()),
      outgoing_services_(std::move(context.incoming_services)),
      listener_binding_(this,
                        std::move(context.session_and_listener_request.second)),
      session_(std::move(context.session_and_listener_request.first)),
      view_(&session_,
            context.view_token2.value
                ? std::move(context.view_token2)
                : scenic::ToViewToken(std::move(context.view_token)),
            debug_name),
      root_node_(&session_) {
  session_.SetDebugName(debug_name);

  // Listen for metrics events on our top node.
  root_node_.SetEventMask(fuchsia::ui::gfx::kMetricsEventMask);
  view_.AddChild(root_node_);

  // We must immediately invalidate the scene, otherwise we wouldn't ever hook
  // the View up to the ViewHolder.  An alternative would be to require
  // subclasses to call an Init() method to set up the initial connection.
  InvalidateScene();
}

void BaseView::SetConfig(fuchsia::ui::views::ViewConfig view_config) {
  if (view_config != view_config_) {
    fuchsia::ui::views::ViewConfig old_config = std::move(view_config_);
    view_config_ = std::move(view_config);
    OnConfigChanged(old_config);
  }
}

void BaseView::SetReleaseHandler(fit::function<void(zx_status_t)> callback) {
  listener_binding_.set_error_handler(std::move(callback));
}

void BaseView::InvalidateScene() {
  TRACE_DURATION("view", "BaseView::InvalidateScene");
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

void BaseView::PresentScene() { PresentScene(last_presentation_time_); }

void BaseView::OnScenicEvent(std::vector<fuchsia::ui::scenic::Event> events) {
  TRACE_DURATION("view", "BaseView::OnScenicEvent");
  for (auto& event : events) {
    switch (event.Which()) {
      case ::fuchsia::ui::scenic::Event::Tag::kGfx:
        switch (event.gfx().Which()) {
          case ::fuchsia::ui::gfx::Event::Tag::kViewPropertiesChanged: {
            auto& evt = event.gfx().view_properties_changed();
            FXL_DCHECK(view_.id() == evt.view_id);
            auto old_props = view_properties_;
            view_properties_ = evt.properties;

            ::fuchsia::ui::gfx::BoundingBox layout_box =
                ViewPropertiesLayoutBox(view_properties_);

            logical_size_ = scenic::Max(layout_box.max - layout_box.min, 0.f);
            physical_size_.x = logical_size_.x * metrics_.scale_x;
            physical_size_.y = logical_size_.y * metrics_.scale_y;
            physical_size_.z = logical_size_.z * metrics_.scale_z;

            OnPropertiesChanged(std::move(old_props));
            InvalidateScene();
            break;
          }
          case fuchsia::ui::gfx::Event::Tag::kMetrics: {
            auto& evt = event.gfx().metrics();
            if (evt.node_id == root_node_.id()) {
              auto old_metrics = metrics_;
              metrics_ = std::move(evt.metrics);
              physical_size_.x = logical_size_.x * metrics_.scale_x;
              physical_size_.y = logical_size_.y * metrics_.scale_y;
              physical_size_.z = logical_size_.z * metrics_.scale_z;
              OnMetricsChanged(std::move(old_metrics));
              InvalidateScene();
            }
            break;
          }
          default: {
            OnScenicEvent(std::move(event));
          }
        }
        break;
      case ::fuchsia::ui::scenic::Event::Tag::kInput: {
        OnInputEvent(std::move(event.input()));
        break;
      }
      case ::fuchsia::ui::scenic::Event::Tag::kUnhandled: {
        OnUnhandledCommand(std::move(event.unhandled()));
        break;
      }
      default: {
        OnScenicEvent(std::move(event));
      }
    }
  }
}

void BaseView::PresentScene(zx_time_t presentation_time) {
  TRACE_DURATION("view", "BaseView::PresentScene");
  FXL_DCHECK(!present_pending_);

  present_pending_ = true;

  // Keep track of the most recent presentation time we've passed to
  // Session.Present(), for use in InvalidateScene().
  last_presentation_time_ = presentation_time;

  TRACE_FLOW_BEGIN("gfx", "Session::Present", session_present_count_);
  ++session_present_count_;

  session()->Present(
      presentation_time, [this](fuchsia::images::PresentationInfo info) {
        TRACE_DURATION("view", "BaseView::PresentationCallback");
        TRACE_FLOW_END("gfx", "present_callback", info.presentation_time);

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

}  // namespace scenic
