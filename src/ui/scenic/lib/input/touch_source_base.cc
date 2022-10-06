// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/input/touch_source_base.h"

#include <lib/async/cpp/time.h>
#include <lib/async/default.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>

#include <unordered_map>

#include "src/lib/fxl/macros.h"

namespace scenic_impl::input {

namespace {

GestureResponse ConvertToGestureResponse(fuchsia::ui::pointer::TouchResponseType type) {
  switch (type) {
    case fuchsia::ui::pointer::TouchResponseType::YES:
      return GestureResponse::kYes;
    case fuchsia::ui::pointer::TouchResponseType::YES_PRIORITIZE:
      return GestureResponse::kYesPrioritize;
    case fuchsia::ui::pointer::TouchResponseType::NO:
      return GestureResponse::kNo;
    case fuchsia::ui::pointer::TouchResponseType::MAYBE:
      return GestureResponse::kMaybe;
    case fuchsia::ui::pointer::TouchResponseType::MAYBE_PRIORITIZE:
      return GestureResponse::kMaybePrioritize;
    case fuchsia::ui::pointer::TouchResponseType::MAYBE_SUPPRESS:
      return GestureResponse::kMaybeSuppress;
    case fuchsia::ui::pointer::TouchResponseType::MAYBE_PRIORITIZE_SUPPRESS:
      return GestureResponse::kMaybePrioritizeSuppress;
    case fuchsia::ui::pointer::TouchResponseType::HOLD:
      return GestureResponse::kHold;
    case fuchsia::ui::pointer::TouchResponseType::HOLD_SUPPRESS:
      return GestureResponse::kHoldSuppress;
    default:
      return GestureResponse::kUndefined;
  }
}

fuchsia::ui::pointer::EventPhase ConvertToEventPhase(Phase phase) {
  switch (phase) {
    case Phase::kAdd:
      return fuchsia::ui::pointer::EventPhase::ADD;
    case Phase::kChange:
      return fuchsia::ui::pointer::EventPhase::CHANGE;
    case Phase::kRemove:
      return fuchsia::ui::pointer::EventPhase::REMOVE;
    case Phase::kCancel:
      return fuchsia::ui::pointer::EventPhase::CANCEL;
    default:
      // Never reached.
      FX_CHECK(false) << "Unknown phase: " << phase;
      return fuchsia::ui::pointer::EventPhase::CANCEL;
  }
}

fuchsia::ui::pointer::TouchEvent NewTouchEvent(StreamId stream_id, const InternalTouchEvent& event,
                                               bool is_end_of_stream) {
  fuchsia::ui::pointer::TouchEvent new_event;
  new_event.set_timestamp(event.timestamp);
  new_event.set_trace_flow_id(TRACE_NONCE());
  {
    fuchsia::ui::pointer::TouchPointerSample pointer;

    pointer.set_phase(ConvertToEventPhase(event.phase));
    pointer.set_position_in_viewport(
        {event.position_in_viewport[0], event.position_in_viewport[1]});
    pointer.set_interaction(fuchsia::ui::pointer::TouchInteractionId{
        .device_id = event.device_id, .pointer_id = event.pointer_id, .interaction_id = stream_id});
    new_event.set_pointer_sample(std::move(pointer));
  }

  return new_event;
}

void AddInteractionResultsToEvent(fuchsia::ui::pointer::TouchEvent& event, StreamId stream_id,
                                  uint32_t device_id, uint32_t pointer_id, const bool awarded_win) {
  event.set_interaction_result(fuchsia::ui::pointer::TouchInteractionResult{
      .interaction =
          fuchsia::ui::pointer::TouchInteractionId{
              .device_id = device_id, .pointer_id = pointer_id, .interaction_id = stream_id},
      .status = awarded_win ? fuchsia::ui::pointer::TouchInteractionStatus::GRANTED
                            : fuchsia::ui::pointer::TouchInteractionStatus::DENIED});
}

fuchsia::ui::pointer::TouchEvent NewEndEvent(StreamId stream_id, uint32_t device_id,
                                             uint32_t pointer_id, bool awarded_win) {
  fuchsia::ui::pointer::TouchEvent new_event;
  new_event.set_timestamp(async::Now(async_get_default_dispatcher()).get());
  new_event.set_trace_flow_id(TRACE_NONCE());
  AddInteractionResultsToEvent(new_event, stream_id, device_id, pointer_id, awarded_win);
  return new_event;
}

void AddViewParametersToEvent(fuchsia::ui::pointer::TouchEvent& event, const Viewport& viewport,
                              view_tree::BoundingBox view_bounds) {
  const auto& [extents, _, receiver_from_viewport_transform] = viewport;
  FX_DCHECK(receiver_from_viewport_transform.has_value());
  event.set_view_parameters(fuchsia::ui::pointer::ViewParameters{
      .view = fuchsia::ui::pointer::Rectangle{.min = view_bounds.min, .max = view_bounds.max},
      .viewport = fuchsia::ui::pointer::Rectangle{.min = {{extents.min[0], extents.min[1]}},
                                                  .max = {{extents.max[0], extents.max[1]}}},
      .viewport_to_view_transform = receiver_from_viewport_transform.value(),
  });
}

bool IsHold(GestureResponse response) {
  switch (response) {
    case GestureResponse::kHold:
    case GestureResponse::kHoldSuppress:
      return true;
    default:
      return false;
  }
}

bool IsHold(fuchsia::ui::pointer::TouchResponseType response) {
  switch (response) {
    case fuchsia::ui::pointer::TouchResponseType::HOLD:
    case fuchsia::ui::pointer::TouchResponseType::HOLD_SUPPRESS:
      return true;
    default:
      return false;
  }
}

}  // namespace

TouchSourceBase::TouchSourceBase(
    zx_koid_t channel_koid, zx_koid_t view_ref_koid,
    fit::function<void(StreamId, const std::vector<GestureResponse>&)> respond,
    fit::function<void(zx_status_t)> close_channel,
    fit::function<void(AugmentedTouchEvent&, const InternalTouchEvent&)> augment,
    GestureContenderInspector& inspector)
    : GestureContender(view_ref_koid),
      channel_koid_(channel_koid),
      respond_(std::move(respond)),
      close_channel_(std::move(close_channel)),
      augment_(std::move(augment)),
      inspector_(inspector) {}

void TouchSourceBase::UpdateStream(StreamId stream_id, const InternalTouchEvent& event,
                                   bool is_end_of_stream, view_tree::BoundingBox view_bounds) {
  const bool is_new_stream = ongoing_streams_.count(stream_id) == 0;
  FX_CHECK(is_new_stream == (event.phase == Phase::kAdd)) << "Stream must only start with ADD.";
  FX_CHECK(is_end_of_stream == (event.phase == Phase::kRemove || event.phase == Phase::kCancel));

  if (is_new_stream) {
    ongoing_streams_.try_emplace(
        stream_id, StreamData{.device_id = event.device_id, .pointer_id = event.pointer_id});
  }
  auto& stream = ongoing_streams_.at(stream_id);
  FX_DCHECK(stream.device_id == event.device_id);
  FX_DCHECK(stream.pointer_id == event.pointer_id);

  // Filter legacy events.
  // TODO(fxbug.dev/53316): Remove when we no longer need to filter events.
  ++stream.num_pointer_events;
  if (event.phase == Phase::kDown || event.phase == Phase::kUp) {
    FX_DCHECK(!is_end_of_stream);
    FX_DCHECK(stream.num_pointer_events > 1);
    stream.filtered_events.emplace(stream.num_pointer_events);
    return;
  }

  {  // Build the event.
    AugmentedTouchEvent out_event;
    {
      out_event.touch_event = NewTouchEvent(stream_id, event, is_end_of_stream);
      auto& touch_event = out_event.touch_event;

      FX_DCHECK(!(won_streams_awaiting_first_message_.count(stream_id) != 0 && !is_new_stream))
          << "Can't have a pre-decided win for an ongoing stream.";
      if (is_new_stream) {
        // First time we see a device we need to add DeviceInfo to the message.
        if (seen_devices_.count(event.device_id) == 0) {
          fuchsia::ui::pointer::TouchDeviceInfo device_info;
          device_info.set_id(event.device_id);
          touch_event.set_device_info(std::move(device_info));

          seen_devices_.emplace(event.device_id);
        }

        // If the stream was won before the first message arrived, attach the "win" to the first
        // message.
        if (won_streams_awaiting_first_message_.count(stream_id) != 0) {
          AddInteractionResultsToEvent(touch_event, stream_id, event.device_id, event.pointer_id,
                                       true);
          won_streams_awaiting_first_message_.erase(stream_id);
        }
      }

      // Add ViewParameters to the message if the viewport or view bounds have changed (which is
      // always true for the first message).
      // (For cancel events it's likely we're not in the view tree, so we can't trust viewport
      //  transforms or view bounds. Skip checking them since it's not necessary at the end of a
      //  stream anyway.)
      if (event.phase != Phase::kCancel &&
          (current_viewport_ != event.viewport || current_view_bounds_ != view_bounds ||
           is_first_event_)) {
        is_first_event_ = false;
        current_viewport_ = event.viewport;
        current_view_bounds_ = view_bounds;
        AddViewParametersToEvent(touch_event, current_viewport_, current_view_bounds_);
      }
    }

    augment_(out_event, event);
    pending_events_.push({.stream_id = stream_id, .event = std::move(out_event)});
  }

  stream.stream_has_ended = is_end_of_stream;
  SendPendingIfWaiting();

  // Cleanup complete stream.
  if (is_end_of_stream && stream.was_won) {
    ongoing_streams_.erase(stream_id);
    FX_DCHECK(won_streams_awaiting_first_message_.count(stream_id) == 0);
  }
}

void TouchSourceBase::EndContest(StreamId stream_id, bool awarded_win) {
  inspector_.OnContestDecided(view_ref_koid_, awarded_win);

  auto it = ongoing_streams_.find(stream_id);
  if (it == ongoing_streams_.end()) {
    if (!awarded_win) {
      // Lost the stream before the first message.
      return;
    }
    const auto [_, success] = won_streams_awaiting_first_message_.emplace(stream_id);
    FX_DCHECK(success) << "Can't have two EndContest() calls for the same stream.";
    return;
  }

  auto& stream = it->second;
  FX_DCHECK(!stream.was_won) << "Can't have two EndContest() calls for the same stream.";
  stream.was_won = awarded_win;
  AugmentedTouchEvent event{
      .touch_event = NewEndEvent(stream_id, stream.device_id, stream.pointer_id, awarded_win)};
  pending_events_.push({.stream_id = stream_id, .event = std::move(event)});
  SendPendingIfWaiting();

  if (!awarded_win) {
    ongoing_streams_.erase(stream_id);
  }
}

zx_status_t TouchSourceBase::ValidateResponses(
    const std::vector<fuchsia::ui::pointer::TouchResponse>& responses,
    const std::vector<ReturnTicket>& return_tickets, bool have_pending_callback) {
  if (have_pending_callback) {
    FX_LOGS(ERROR) << "TouchSourceBase: Client called Watch twice without waiting for response.";
    return ZX_ERR_BAD_STATE;
  }

  if (return_tickets.size() != responses.size()) {
    FX_LOGS(ERROR)
        << "TouchSourceBase: Client called Watch with the wrong number of responses. Expected: "
        << return_tickets.size() << " Received: " << responses.size();
    return ZX_ERR_INVALID_ARGS;
  }

  for (size_t i = 0; i < responses.size(); ++i) {
    const auto& response = responses.at(i);
    if (!return_tickets.at(i).expects_response) {
      if (!response.IsEmpty()) {
        FX_LOGS(ERROR) << "TouchSourceBase: Expected empty response, receive non-empty response";
        return ZX_ERR_INVALID_ARGS;
      }
    } else {
      if (!response.has_response_type()) {
        FX_LOGS(ERROR) << "TouchSourceBase: Response was missing arguments.";
        return ZX_ERR_INVALID_ARGS;
      }

      if (ConvertToGestureResponse(response.response_type()) == GestureResponse::kUndefined) {
        FX_LOGS(ERROR) << "TouchSourceBase: Response " << i << " had unknown response type.";
        return ZX_ERR_INVALID_ARGS;
      }
    }
  }

  return ZX_OK;
}

void TouchSourceBase::WatchBase(std::vector<fuchsia::ui::pointer::TouchResponse> responses,
                                fit::function<void(std::vector<AugmentedTouchEvent>)> callback) {
  TRACE_DURATION("input", "TouchSourceBase::Watch");
  const zx_status_t error = ValidateResponses(
      responses, return_tickets_, /*have_pending_callback*/ pending_callback_ != nullptr);
  if (error != ZX_OK) {
    close_channel_(error);
    return;
  }

  // De-interlace responses from different streams.
  std::unordered_map<StreamId, std::vector<GestureResponse>> responses_per_stream;
  size_t index = 0;
  for (const auto& response : responses) {
    if (response.has_trace_flow_id()) {
      TRACE_FLOW_END("input", "received_response", response.trace_flow_id());
    }

    const auto [stream_id, expects_response] = return_tickets_.at(index++);
    if (!expects_response || ongoing_streams_.count(stream_id) == 0) {
      continue;
    }

    const GestureResponse gd_response = ConvertToGestureResponse(response.response_type());
    responses_per_stream[stream_id].emplace_back(gd_response);

    auto& stream = ongoing_streams_[stream_id];
    stream.last_response = gd_response;

    // TODO(fxbug.dev/53316): Remove when we no longer need to filter events.
    // Duplicate the response for any subsequent filtered events.
    ++stream.num_responses;
    while (!stream.filtered_events.empty() &&
           stream.num_responses == stream.filtered_events.front() - 1) {
      ++stream.num_responses;
      stream.filtered_events.pop();
      responses_per_stream[stream_id].emplace_back(gd_response);
    }
  }

  for (const auto& [stream_id, gd_responses] : responses_per_stream) {
    respond_(stream_id, gd_responses);
  }

  pending_callback_ = std::move(callback);
  return_tickets_.clear();
  SendPendingIfWaiting();
}

zx_status_t TouchSourceBase::ValidateUpdateResponse(
    const fuchsia::ui::pointer::TouchInteractionId& stream_identifier,
    const fuchsia::ui::pointer::TouchResponse& response,
    const std::unordered_map<StreamId, StreamData>& ongoing_streams) {
  const StreamId stream_id = stream_identifier.interaction_id;
  if (ongoing_streams.count(stream_id) == 0) {
    FX_LOGS(ERROR)
        << "TouchSourceBase: Attempted to UpdateResponse for unkown stream. Received stream id: "
        << stream_id;
    return ZX_ERR_BAD_STATE;
  }

  if (!response.has_response_type()) {
    FX_LOGS(ERROR)
        << "TouchSourceBase: Can only UpdateResponse() called without response_type argument.";
    return ZX_ERR_INVALID_ARGS;
  }

  if (IsHold(response.response_type())) {
    FX_LOGS(ERROR) << "TouchSourceBase: Can only UpdateResponse() with non-HOLD response.";
    return ZX_ERR_INVALID_ARGS;
  }

  const auto& stream = ongoing_streams.at(stream_id);
  if (!IsHold(stream.last_response)) {
    FX_LOGS(ERROR) << "TouchSourceBase: Can only UpdateResponse() if previous response was HOLD.";
    return ZX_ERR_BAD_STATE;
  }

  if (!stream.stream_has_ended) {
    FX_LOGS(ERROR) << "TouchSourceBase: Can only UpdateResponse() for ended streams.";
    return ZX_ERR_BAD_STATE;
  }

  return ZX_OK;
}

void TouchSourceBase::UpdateResponseBase(fuchsia::ui::pointer::TouchInteractionId stream_identifier,
                                         fuchsia::ui::pointer::TouchResponse response,
                                         fit::function<void()> callback) {
  TRACE_DURATION("input", "TouchSourceBase::UpdateResponse");
  const zx_status_t error = ValidateUpdateResponse(stream_identifier, response, ongoing_streams_);
  if (error != ZX_OK) {
    close_channel_(error);
    return;
  }

  if (response.has_trace_flow_id()) {
    TRACE_FLOW_END("input", "received_response", response.trace_flow_id());
  }

  const StreamId stream_id = stream_identifier.interaction_id;
  const GestureResponse converted_response = ConvertToGestureResponse(response.response_type());
  ongoing_streams_.at(stream_id).last_response = converted_response;
  respond_(stream_id, {converted_response});

  callback();
}

void TouchSourceBase::SendPendingIfWaiting() {
  if (!pending_callback_ || pending_events_.empty()) {
    return;
  }
  FX_DCHECK(return_tickets_.empty());

  std::vector<AugmentedTouchEvent> events;
  for (size_t i = 0; !pending_events_.empty() && i < fuchsia::ui::pointer::TOUCH_MAX_EVENT; ++i) {
    auto [stream_id, event] = std::move(pending_events_.front());
    TRACE_FLOW_BEGIN("input", "dispatch_event_to_client", event.touch_event.trace_flow_id());

    pending_events_.pop();
    return_tickets_.push_back(
        {.stream_id = stream_id, .expects_response = event.touch_event.has_pointer_sample()});
    events.emplace_back(std::move(event));
  }
  FX_DCHECK(!events.empty());
  FX_DCHECK(events.size() == return_tickets_.size());

  inspector_.OnInjectedEvents(view_ref_koid_, events.size());
  pending_callback_(std::move(events));
  pending_callback_ = nullptr;
}

}  // namespace scenic_impl::input
