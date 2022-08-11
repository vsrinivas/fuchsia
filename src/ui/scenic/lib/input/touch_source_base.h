// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_INPUT_TOUCH_SOURCE_BASE_H_
#define SRC_UI_SCENIC_LIB_INPUT_TOUCH_SOURCE_BASE_H_

#include <lib/fit/function.h>
#include <lib/inspect/cpp/inspect.h>

#include <queue>
#include <unordered_map>
#include <unordered_set>

#include "src/lib/fxl/macros.h"
#include "src/ui/scenic/lib/input/gesture_contender.h"
#include "src/ui/scenic/lib/input/gesture_contender_inspector.h"
#include "src/ui/scenic/lib/view_tree/snapshot_types.h"

namespace scenic_impl::input {

// Base class for implementations of fuchsia.ui.pointer.TouchSource and its augmentations.
class TouchSourceBase : public GestureContender {
 public:
  ~TouchSourceBase() override = default;

  // |GestureContender|
  // For |view_bounds| and |event.viewport| new values are only sent to the client when they've
  // changed from their last seen values.
  void UpdateStream(StreamId stream_id, const InternalTouchEvent& event, bool is_end_of_stream,
                    view_tree::BoundingBox view_bounds) override;

  // |GestureContender|
  void EndContest(StreamId stream_id, bool awarded_win) override;

  zx_koid_t channel_koid() const override { return channel_koid_; }

 protected:
  // Augmentation data for f.u.p.augment.TouchEventWithLocalHit.
  struct LocalHit {
    zx_koid_t local_viewref_koid;
    std::array<float, 2> local_point;
  };

  // Struct for holding the touch event and any potential augmentations.
  struct AugmentedTouchEvent {
    // Base event.
    fuchsia::ui::pointer::TouchEvent touch_event;

    // Possible augmentation data.
    std::optional<LocalHit> local_hit;
  };

  // |respond_| must not destroy the TouchSourceBase object.
  TouchSourceBase(zx_koid_t channel_koid, zx_koid_t view_ref_koid,
                  fit::function<void(StreamId, const std::vector<GestureResponse>&)> respond,
                  fit::function<void(zx_status_t)> close_channel,
                  fit::function<void(AugmentedTouchEvent&, const InternalTouchEvent&)> augment,
                  GestureContenderInspector& inspector);

  void WatchBase(std::vector<fuchsia::ui::pointer::TouchResponse> responses,
                 fit::function<void(std::vector<AugmentedTouchEvent>)> callback);

  void UpdateResponseBase(fuchsia::ui::pointer::TouchInteractionId stream,
                          fuchsia::ui::pointer::TouchResponse response,
                          fit::function<void()> callback);

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
    AugmentedTouchEvent event;
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

  const zx_koid_t channel_koid_;
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

  const fit::function<void(StreamId, const std::vector<GestureResponse>&)> respond_;
  // Closes the fidl channel. This triggers the destruction of the TouchSourceBase object through
  // the error handler set in InputSystem. NOTE: No further method calls or member accesses should
  // be made after close_channel_(), since they might be made on a destroyed object.
  const fit::function<void(zx_status_t epitaph)> close_channel_;

  // Used by some subtypes to add augmentations to each event.
  const fit::function<void(AugmentedTouchEvent&, const InternalTouchEvent&)> augment_;

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

  fit::function<void(std::vector<AugmentedTouchEvent>)> pending_callback_ = nullptr;

  // Saved by reference since |inspector_| is guaranteed to outlive the contender.
  GestureContenderInspector& inspector_;
};

}  // namespace scenic_impl::input

#endif  // SRC_UI_SCENIC_LIB_INPUT_TOUCH_SOURCE_BASE_H_
