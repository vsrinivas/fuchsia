// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/input/mouse_source_base.h"

#include <lib/async/cpp/time.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>

namespace scenic_impl::input {

using fuchsia::ui::pointer::MouseEvent;

namespace {

MouseEvent NewMouseEvent(const InternalMouseEvent& event) {
  MouseEvent new_event;
  new_event.set_timestamp(event.timestamp);
  new_event.set_trace_flow_id(TRACE_NONCE());
  new_event.set_pointer_sample(MouseSourceBase::NewPointerSample(event));
  return new_event;
}

void AddDeviceInfoToEvent(MouseEvent& out_event, const InternalMouseEvent& event) {
  fuchsia::ui::pointer::MouseDeviceInfo device_info;
  device_info.set_id(event.device_id);
  if (event.scroll_v.has_value()) {
    const auto& [unit, exponent, range, _] = event.scroll_v.value();
    FX_DCHECK(range[0] < range[1]);
    device_info.set_scroll_v_range({.range = {.min = range[0], .max = range[1]},
                                    .unit = {.type = unit, .exponent = exponent}});
  }
  if (event.scroll_h.has_value()) {
    const auto& [unit, exponent, range, _] = event.scroll_h.value();
    FX_DCHECK(range[0] < range[1]);
    device_info.set_scroll_h_range({.range = {.min = range[0], .max = range[1]},
                                    .unit = {.type = unit, .exponent = exponent}});
  }
  if (!event.buttons.identifiers.empty()) {
    FX_DCHECK(event.buttons.identifiers.size() <= fuchsia::input::report::MOUSE_MAX_NUM_BUTTONS);
    device_info.set_buttons(event.buttons.identifiers);
  }
  out_event.set_device_info(std::move(device_info));
}

void AddStreamInfoToEvent(MouseEvent& out_event, const InternalMouseEvent& event,
                          bool view_entered) {
  out_event.set_stream_info({.device_id = event.device_id,
                             .status = view_entered
                                           ? fuchsia::ui::pointer::MouseViewStatus::ENTERED
                                           : fuchsia::ui::pointer::MouseViewStatus::EXITED});
}

void AddViewParametersToEvent(MouseEvent& out_event, const Viewport& viewport,
                              const view_tree::BoundingBox& view_bounds) {
  out_event.set_view_parameters(
      fuchsia::ui::pointer::ViewParameters{
          .view = fuchsia::ui::pointer::Rectangle{.min = view_bounds.min, .max = view_bounds.max},
          .viewport =
              fuchsia::ui::pointer::Rectangle{
                  .min = {{viewport.extents.min[0], viewport.extents.min[1]}},
                  .max = {{viewport.extents.max[0], viewport.extents.max[1]}}},
          .viewport_to_view_transform = viewport.receiver_from_viewport_transform.value(),
      });
}

MouseEvent NewViewExitEvent(const InternalMouseEvent& event) {
  MouseEvent new_event;
  new_event.set_timestamp(event.timestamp);
  new_event.set_trace_flow_id(TRACE_NONCE());
  new_event.set_stream_info(
      {.device_id = event.device_id, .status = fuchsia::ui::pointer::MouseViewStatus::EXITED});
  return new_event;
}

}  // namespace

void MouseSourceBase::WatchBase(fit::function<void(std::vector<MouseEvent>)> callback) {
  TRACE_DURATION("input", "MouseSourceBase::Watch");
  if (pending_callback_ != nullptr) {
    FX_LOGS(ERROR) << "Called Watch() without waiting for previous callback to return.";
    close_channel_(ZX_ERR_BAD_STATE);
    return;
  }

  pending_callback_ = std::move(callback);
  SendPendingIfWaiting();
}

fuchsia::ui::pointer::MousePointerSample MouseSourceBase::NewPointerSample(
    const InternalMouseEvent& event) {
  fuchsia::ui::pointer::MousePointerSample pointer;
  pointer.set_device_id(event.device_id);

  pointer.set_position_in_viewport({event.position_in_viewport[0], event.position_in_viewport[1]});

  if (event.scroll_v.has_value() && event.scroll_v->scroll_value.has_value()) {
    pointer.set_scroll_v(event.scroll_v->scroll_value.value());
  }
  if (event.scroll_h.has_value() && event.scroll_h->scroll_value.has_value()) {
    pointer.set_scroll_h(event.scroll_h->scroll_value.value());
  }

  FX_DCHECK(event.buttons.pressed.size() <= fuchsia::input::report::MOUSE_MAX_NUM_BUTTONS);
  if (!event.buttons.pressed.empty()) {
    pointer.set_pressed_buttons(event.buttons.pressed);
  }

  return pointer;
}

void MouseSourceBase::UpdateStream(const StreamId stream_id, const InternalMouseEvent& event,
                                   const view_tree::BoundingBox view_bounds, const bool view_exit) {
  if (view_exit) {
    const auto erased = tracked_streams_.erase(stream_id);
    FX_DCHECK(erased == 1) << "First event of a stream can't have MouseViewStatus::EXITED";
    pending_events_.push(NewViewExitEvent(event));
    SendPendingIfWaiting();
    return;
  }

  auto out_event = NewMouseEvent(event);
  if (tracked_streams_.count(stream_id) == 0) {
    tracked_streams_.emplace(stream_id);
    AddStreamInfoToEvent(out_event, event, /*view_entered*/ true);
  }

  if (tracked_devices_.count(event.device_id) == 0) {
    tracked_devices_.emplace(event.device_id);
    AddDeviceInfoToEvent(out_event, event);
  }

  // Add ViewParameters to the message if the viewport or view bounds have changed (which is
  // always true for the first message).
  if (current_viewport_ != event.viewport || current_view_bounds_ != view_bounds ||
      is_first_event_) {
    is_first_event_ = false;
    current_viewport_ = event.viewport;
    current_view_bounds_ = view_bounds;
    AddViewParametersToEvent(out_event, current_viewport_, current_view_bounds_);
  }

  pending_events_.push(std::move(out_event));
  SendPendingIfWaiting();
}

void MouseSourceBase::SendPendingIfWaiting() {
  if (!pending_callback_ || pending_events_.empty()) {
    return;
  }

  std::vector<MouseEvent> events;
  for (size_t i = 0; !pending_events_.empty() && i < fuchsia::ui::pointer::MOUSE_MAX_EVENT; ++i) {
    auto event = std::move(pending_events_.front());
    TRACE_FLOW_BEGIN("input", "dispatch_event_to_client", event.trace_flow_id());

    pending_events_.pop();
    events.emplace_back(std::move(event));
  }
  FX_DCHECK(!events.empty());
  FX_DCHECK(events.size() <= fuchsia::ui::pointer::MOUSE_MAX_EVENT);

  // Move the callback onto the stack and reset the member variable before calling it.
  // This is to allow the |pending_callback_| member variable to be reset from within the callback.
  auto pending_callback = std::move(pending_callback_);
  pending_callback_ = nullptr;
  pending_callback(std::move(events));
}

}  // namespace scenic_impl::input
