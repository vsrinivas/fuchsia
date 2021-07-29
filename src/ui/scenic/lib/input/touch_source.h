// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_INPUT_TOUCH_SOURCE_H_
#define SRC_UI_SCENIC_LIB_INPUT_TOUCH_SOURCE_H_

#include <fuchsia/ui/pointer/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fit/function.h>

#include <queue>
#include <unordered_map>
#include <unordered_set>

#include "src/ui/scenic/lib/input/gesture_contender.h"
#include "src/ui/scenic/lib/view_tree/snapshot_types.h"

namespace scenic_impl::input {

// Implementation of the |fuchsia::ui::pointer::TouchSource| interface. One instance per
// channel.
class TouchSource : public GestureContender, public fuchsia::ui::pointer::TouchSource {
 public:
  // |respond_| must not destroy the TouchSource object.
  TouchSource(zx_koid_t view_ref_koid,
              fidl::InterfaceRequest<fuchsia::ui::pointer::TouchSource> event_provider,
              fit::function<void(StreamId, const std::vector<GestureResponse>&)> respond,
              fit::function<void()> error_handler);

  ~TouchSource() override;

  // |GestureContender|
  // For |view_bounds| |event.viewport| new values are only sent to the client when they've changed
  // from their previous seen values.
  void UpdateStream(StreamId stream_id, const InternalPointerEvent& event, bool is_end_of_stream,
                    view_tree::BoundingBox view_bounds) override;

  // |GestureContender|
  void EndContest(StreamId stream_id, bool awarded_win) override;

  // |fuchsia::ui::pointer::TouchSource|
  void Watch(std::vector<fuchsia::ui::pointer::TouchResponse> responses,
             WatchCallback callback) override;

  // |fuchsia::ui::pointer::TouchSource|
  void UpdateResponse(fuchsia::ui::pointer::TouchInteractionId stream,
                      fuchsia::ui::pointer::TouchResponse response,
                      UpdateResponseCallback callback) override;

  // TODO(fxbug.dev/78951): Implement ANR.

 private:
  struct StreamData {
    uint32_t device_id = 0;
    uint32_t pointer_id = 0;
    bool stream_has_ended = false;
    bool was_won = false;
    GestureResponse last_response = GestureResponse::kUndefined;

    // TODO(fxbug.dev/53316): Remove when we no longer need to filter events. Keeps indexes into
    // duplicate events for legacy injectors.
    uint64_t num_pointer_events = 0;
    uint64_t num_responses = 0;
    std::queue<uint64_t> filtered_events;
  };

  // Used to track expected responses from the client for each sent event.
  struct ReturnTicket {
    StreamId stream_id = kInvalidStreamId;
    bool expects_response = false;
  };

  // Used to track events awaiting Watch() calls.
  struct PendingEvent {
    StreamId stream_id = kInvalidStreamId;
    fuchsia::ui::pointer::TouchEvent event;
  };

  void SendPendingIfWaiting();

  // Checks that the input is valid for the current state. If not valid it returns the error string
  // to print and the epitaph to send on the channel when closing.
  static zx_status_t ValidateResponses(
      const std::vector<fuchsia::ui::pointer::TouchResponse>& responses,
      const std::vector<ReturnTicket>& last_messages, bool have_pending_callback);
  static zx_status_t ValidateUpdateResponse(
      const fuchsia::ui::pointer::TouchInteractionId& stream_identifier,
      const fuchsia::ui::pointer::TouchResponse& response,
      const std::unordered_map<StreamId, StreamData>& ongoing_streams);

  // Closes the fidl channel. This triggers the destruction of the TouchSource object through
  // the error handler set in InputSystem. NOTE: No further method calls or member accesses should
  // be made after CloseChannel(), since they might be made on a destroyed object.
  void CloseChannel(zx_status_t epitaph);

  bool is_first_event_ = true;
  Viewport current_viewport_;
  view_tree::BoundingBox current_view_bounds_;

  // Events waiting to be sent to client. Sent in batches of up to
  // fuchsia::ui::pointer::TOUCH_MAX_EVENT events on each call to Watch().
  std::queue<PendingEvent> pending_events_;
  // When a vector of events is sent out in response to a Watch() call, the next Watch() call must
  // contain responses matching the previous set of events. |return_tickets_| tracks the expected
  // responses for the previous set of events.
  std::vector<ReturnTicket> return_tickets_;

  fidl::Binding<fuchsia::ui::pointer::TouchSource> binding_;
  const fit::function<void(StreamId, const std::vector<GestureResponse>&)> respond_;
  const fit::function<void()> error_handler_;

  // Tracks all streams that have had at least one event passed into UpdateStream(), and that
  // haven't either "been won and has ended", or "haven't been lost".
  std::unordered_map<StreamId, StreamData> ongoing_streams_;

  // Tracks all the devices that have previously been seen, to determine when we need to provide
  // a TouchInteractionId value.
  std::unordered_set<uint32_t> seen_devices_;

  // Streams can be declared as won before the first UpdateStream() call concerning the stream,
  // this set tracks those streams. This set should never contain a stream that also exists in
  // |ongoing_streams_|.
  std::unordered_set<StreamId> won_streams_awaiting_first_message_;

  WatchCallback pending_callback_ = nullptr;
};

}  // namespace scenic_impl::input

#endif  // SRC_UI_SCENIC_LIB_INPUT_TOUCH_SOURCE_H_
