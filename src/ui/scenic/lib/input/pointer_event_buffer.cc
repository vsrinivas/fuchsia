// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/input/pointer_event_buffer.h"

namespace scenic_impl {
namespace input {

using AccessibilityPointerEvent = fuchsia::ui::input::accessibility::PointerEvent;

PointerEventBuffer::PointerEventBuffer(DispatchEventFunction dispatch_events,
                                       ReportAccessibilityEventFunction report_to_accessibility)
    : dispatch_events_(std::move(dispatch_events)),
      report_to_accessibility_(report_to_accessibility) {}

PointerEventBuffer::~PointerEventBuffer() {
  // Any remaining pointer events are dispatched to clients to keep a consistent state.
  for (auto& pointer_id_and_streams : buffer_) {
    for (auto& stream : pointer_id_and_streams.second) {
      for (auto& deferred_events : stream.serial_events) {
        dispatch_events_(std::move(deferred_events));
      }
    }
  }
}

void PointerEventBuffer::UpdateStream(uint32_t pointer_id,
                                      fuchsia::ui::input::accessibility::EventHandling handled) {
  auto it = buffer_.find(pointer_id);
  if (it == buffer_.end()) {
    // Empty buffer for this pointer id. Simply return.
    return;
  }
  auto& pointer_id_buffer = it->second;
  if (pointer_id_buffer.empty()) {
    // there are no streams left.
    return;
  }
  auto& stream = pointer_id_buffer.front();
  PointerIdStreamStatus status = PointerIdStreamStatus::WAITING_RESPONSE;
  switch (handled) {
    case fuchsia::ui::input::accessibility::EventHandling::CONSUMED:
      status = PointerIdStreamStatus::CONSUMED;
      break;
    case fuchsia::ui::input::accessibility::EventHandling::REJECTED:
      // the accessibility listener rejected this stream of pointer events
      // related to this pointer id. They follow their normal flow and are
      // sent to views. All buffered (past events), are sent, as well as
      // potential future (in case this stream is not done yet).
      status = PointerIdStreamStatus::REJECTED;
      for (auto& deferred_events : stream.serial_events) {
        dispatch_events_(std::move(deferred_events));
      }
      // Clears the stream -- objects have been moved, but container still holds
      // their space.
      stream.serial_events.clear();
      break;
  };
  // Remove this stream from the buffer, as it was already processed.
  pointer_id_buffer.pop_front();
  // If the buffer is now empty, this means that this stream hasn't finished
  // yet. Record this so that incoming future pointer events know where to go.
  // Please note that if the buffer is not empty, this means that there are
  // streams waiting for a response, thus, this is not the active stream
  // anymore. If this is the case, |active_stream_info_| will not be updated
  // and thus will still have a status of WAITING_RESPONSE.
  if (pointer_id_buffer.empty()) {
    SetActiveStreamInfo(pointer_id, status);
  }
  FXL_DCHECK(pointer_id_buffer.empty() ||
             active_stream_info_[pointer_id] == PointerIdStreamStatus::WAITING_RESPONSE)
      << "invariant: streams are waiting, so status is waiting";
}

void PointerEventBuffer::AddEvent(uint32_t pointer_id, DeferredPointerEvent views_and_event,
                                  AccessibilityPointerEvent accessibility_pointer_event) {
  auto it = active_stream_info_.find(pointer_id);
  FXL_DCHECK(it != active_stream_info_.end()) << "Received an invalid pointer id.";
  const auto status = it->second;
  if (status == PointerIdStreamStatus::WAITING_RESPONSE) {
    PointerIdStream& stream = buffer_[pointer_id].back();
    stream.serial_events.emplace_back(std::move(views_and_event));
  } else if (status == PointerIdStreamStatus::REJECTED) {
    // All previous events were already dispatched when this stream was
    // rejected. Sends this new incoming events to their normal flow as well.
    // There is still the possibility of triggering a focus change event, when
    // ADD -> a11y listener rejected -> DOWN event arrived.
    dispatch_events_(std::move(views_and_event));
    return;
  }
  // PointerIdStreamStatus::CONSUMED or PointerIdStreamStatus::WAITING_RESPONSE
  // follow the same path: accessibility listener needs to see the pointer event
  // to consume / decide if it will consume them.
  if (status == PointerIdStreamStatus::WAITING_RESPONSE ||
      status == PointerIdStreamStatus::CONSUMED) {
    report_to_accessibility_(std::move(accessibility_pointer_event));
  }
}

void PointerEventBuffer::AddStream(uint32_t pointer_id) {
  auto& pointer_id_buffer = buffer_[pointer_id];
  pointer_id_buffer.emplace_back();
  active_stream_info_[pointer_id] = PointerIdStreamStatus::WAITING_RESPONSE;
}

}  // namespace input
}  // namespace scenic_impl
