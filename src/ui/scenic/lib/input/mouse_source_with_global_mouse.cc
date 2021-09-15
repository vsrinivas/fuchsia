// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/input/mouse_source_with_global_mouse.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>

#include "src/ui/scenic/lib/utils/helpers.h"

namespace scenic_impl::input {

using fuchsia::ui::pointer::MouseEvent;
using fuchsia::ui::pointer::augment::MouseEventWithGlobalMouse;

MouseSourceWithGlobalMouse::MouseSourceWithGlobalMouse(
    fidl::InterfaceRequest<fuchsia::ui::pointer::augment::MouseSourceWithGlobalMouse> mouse_source,
    fit::function<void()> error_handler)
    : MouseSourceBase(utils::ExtractKoid(mouse_source.channel()), /*close_channel=*/
                      [this](zx_status_t epitaph) {
                        binding_.Close(epitaph);
                        error_handler_();
                      }),
      binding_(this, std::move(mouse_source)),
      error_handler_(std::move(error_handler)) {
  binding_.set_error_handler([this](zx_status_t) { error_handler_(); });
  CollectBaseEvents();
}

void MouseSourceWithGlobalMouse::Watch(WatchCallback callback) {
  TRACE_DURATION("input", "MouseSourceWithGlobalMouse::Watch");
  if (pending_callback_ != nullptr) {
    FX_LOGS(ERROR) << "Called Watch() without waiting for previous callback to return.";
    error_handler_();
    return;
  }

  pending_callback_ = std::move(callback);
  SendPendingIfWaiting();
}

void MouseSourceWithGlobalMouse::CollectBaseEvents() {
  MouseSourceBase::WatchBase(
      [this](std::vector<MouseEvent> events) {
        FX_DCHECK(events.size() == 1)
            << "Should receive at most one regular event for every global event";
        FX_DCHECK(!last_base_event_.has_value())
            << "Must call AddGlobalEvent() or SendLocalEvent() at least once between each call to "
               "UpdateStream()";
        last_base_event_.emplace(std::move(events.front()));
        CollectBaseEvents();
      });
}

void MouseSourceWithGlobalMouse::AddGlobalEvent(const InternalMouseEvent& event,
                                                const bool inside_view) {
  const uint32_t device_id = event.device_id;
  const bool add_global_event = pointers_inside_view_.count(device_id) != 0 || inside_view;
  const bool add_local_event = last_base_event_.has_value();
  if (!add_global_event && !add_local_event) {
    return;
  }

  MouseEventWithGlobalMouse& out_event = pending_events_.emplace();

  if (add_global_event) {
    out_event.set_global_position(MouseSourceBase::NewPointerSample(event));

    // Add |global_stream_info| if the view hover state has changed.
    const bool pointer_already_inside_view = pointers_inside_view_.count(device_id) != 0;
    if (pointer_already_inside_view != inside_view) {
      out_event.set_global_stream_info(fuchsia::ui::pointer::MouseEventStreamInfo{
          .device_id = device_id,
          .status = inside_view ? fuchsia::ui::pointer::MouseViewStatus::ENTERED
                                : fuchsia::ui::pointer::MouseViewStatus::EXITED,
      });
      if (inside_view) {
        const auto [_, success] = pointers_inside_view_.emplace(device_id);
        FX_DCHECK(success);
      } else {
        const auto erased = pointers_inside_view_.erase(device_id);
        FX_DCHECK(erased == 1);
      }
    }
  }

  if (add_local_event) {
    out_event.set_mouse_event(std::move(last_base_event_.value()));
    last_base_event_ = std::nullopt;
  }

  SendPendingIfWaiting();
}

void MouseSourceWithGlobalMouse::SendPendingIfWaiting() {
  if (!pending_callback_ || pending_events_.empty()) {
    return;
  }

  std::vector<MouseEventWithGlobalMouse> events;
  for (size_t i = 0; !pending_events_.empty() && i < fuchsia::ui::pointer::MOUSE_MAX_EVENT; ++i) {
    auto& event = pending_events_.front();
    if (event.has_mouse_event()) {
      TRACE_FLOW_BEGIN("input", "dispatch_event_to_client", event.mouse_event().trace_flow_id());
    }
    events.emplace_back(std::move(event));
    pending_events_.pop();
  }
  FX_DCHECK(!events.empty());
  FX_DCHECK(events.size() <= fuchsia::ui::pointer::MOUSE_MAX_EVENT);

  pending_callback_(std::move(events));
  pending_callback_ = nullptr;
}

}  // namespace scenic_impl::input
