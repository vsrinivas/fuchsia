// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_INPUT_POINTER_EVENT_BUFFER_H_
#define SRC_UI_SCENIC_LIB_INPUT_POINTER_EVENT_BUFFER_H_

#include <fuchsia/ui/input/accessibility/cpp/fidl.h>
#include <fuchsia/ui/input/cpp/fidl.h>

#include <deque>
#include <unordered_map>
#include <vector>

#include "src/ui/scenic/lib/input/view_stack.h"

namespace scenic_impl {
namespace input {

// A buffer to store pointer events.
//
// This buffer is used when an accessibility listener is intercepting pointer events.
// It sends each event to the accessibility listener, buffering streams of them until
// accessibility decides to either reject or consume a stream.
// If rejected, the events of the stream (past and future) are sent directly to the views that would
// normally receive them. If consumed, all events for the stream are sent only to accessibility.
class PointerEventBuffer {
 public:
  // Captures the deferred parallel dispatch of a pointer event.
  struct DeferredPointerEvent {
    fuchsia::ui::input::PointerEvent event;
    // Position 0 of the vector holds the top-most view. The vector may be empty.
    std::vector<ViewStack::Entry> parallel_event_receivers;
  };
  // Represents a stream of pointer events. A stream is a sequence of pointer events with phase
  // ADD -> * -> REMOVE.
  struct PointerIdStream {
    // The temporally-ordered pointer events of this stream. Each element of this vector (indexed
    // by time) contains another vector (indexed by view); one touch event may be dispatched
    // multiple times, to multiple views, in parallel (simultaneously).
    std::vector<DeferredPointerEvent> serial_events;
  };
  // Possible states of a stream.
  enum PointerIdStreamStatus {
    WAITING_RESPONSE = 0,  // accessibility listener hasn't responded yet.
    CONSUMED = 1,
    REJECTED = 2,
  };

  using ReportAccessibilityEventFunction =
      std::function<void(fuchsia::ui::input::accessibility::PointerEvent)>;
  using DispatchEventFunction = fit::function<void(PointerEventBuffer::DeferredPointerEvent)>;

  PointerEventBuffer(DispatchEventFunction dispatch_events,
                     ReportAccessibilityEventFunction report_to_accessibility);
  ~PointerEventBuffer();

  // Adds a parallel dispatch event list |views_and_event| to the latest
  // stream associated with |pointer_id|. It also takes
  // |accessibility_pointer_event|, which is sent to the listener depending on
  // the current stream status.
  void AddEvent(uint32_t pointer_id, DeferredPointerEvent views_and_event,
                fuchsia::ui::input::accessibility::PointerEvent accessibility_pointer_event);

  // Adds a new stream associated with |pointer_id|.
  void AddStream(uint32_t pointer_id);

  // Updates the oldest stream associated with |pointer_id|, triggering an
  // appropriate action depending on |handled|.
  // If |handled| == CONSUMED, continues sending events to the listener.
  // If |handled| == REJECTED, dispatches buffered pointer events to views.
  void UpdateStream(uint32_t pointer_id, fuchsia::ui::input::accessibility::EventHandling handled);

  // Sets the status of view of the active stream for a pointer ID.
  void SetActiveStreamInfo(uint32_t pointer_id, PointerIdStreamStatus status) {
    active_stream_info_[pointer_id] = {status};
  }

 private:
  // NOTE: We assume there is one touch screen, and hence unique pointer IDs.
  // key = pointer ID, value = a list of pointer streams. Every new stream is
  // added to the end of the list, where a consume / reject response from the
  // listener always removes the first element.
  std::unordered_map<uint32_t, std::deque<PointerIdStream>> buffer_;

  // Key = pointer ID, value = the status of the current active stream.
  //
  // This is kept separate from the map above because this must outlive
  // the stream itself. When the accessibility listener responds, the first
  // non-processed stream is consumed / rejected and gets removed from the
  // buffer. It may not be finished (we haven't seen a pointer event with
  // phase == REMOVE), so it is necessary to still keep track of where the
  // incoming pointer events should go, although they don't need to be
  // buffered anymore.
  //
  // Whenever a pointer ID is added, its default value is WAITING_RESPONSE.
  std::unordered_map</*pointer ID*/ uint32_t, PointerIdStreamStatus> active_stream_info_;

  DispatchEventFunction dispatch_events_;
  ReportAccessibilityEventFunction report_to_accessibility_;
};

}  // namespace input
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_INPUT_POINTER_EVENT_BUFFER_H_
