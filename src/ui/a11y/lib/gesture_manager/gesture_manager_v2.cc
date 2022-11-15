// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/gesture_manager/gesture_manager_v2.h"

#include <fuchsia/ui/input/accessibility/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>

#include <cstdint>
#include <memory>
#include <optional>

#include "fuchsia/ui/pointer/cpp/fidl.h"
#include "src/ui/a11y/lib/gesture_manager/arena_v2/gesture_arena_v2.h"

namespace a11y {
namespace {

using fuchsia::ui::pointer::EventPhase;
using fuchsia::ui::pointer::TouchResponse;
using fuchsia::ui::pointer::TouchResponseType;
using fuchsia::ui::pointer::augment::TouchEventWithLocalHit;
using fuchsia::ui::pointer::augment::TouchSourceWithLocalHitPtr;

// Based on the status of the current a11y gesture arena contest, how should we
// respond in the system-level gesture disambiguation.
//
// Note that this is only the initial response; sometimes we'll have to say
// "hold" to indicate we don't know whether this interaction is ours yet. Once
// the current a11y gesture arena contest completes, we go back and update our
// responses.
TouchResponseType initialResponse(InteractionTracker::ConsumptionStatus status, EventPhase phase) {
  switch (status) {
    case InteractionTracker::ConsumptionStatus::kAccept:
      return TouchResponseType::YES_PRIORITIZE;
    case InteractionTracker::ConsumptionStatus::kReject:
      return TouchResponseType::NO;
    case InteractionTracker::ConsumptionStatus::kUndecided:
      switch (phase) {
        case EventPhase::ADD:
        case EventPhase::CHANGE:
          return TouchResponseType::MAYBE_PRIORITIZE_SUPPRESS;
        case EventPhase::REMOVE:
        case EventPhase::CANCEL:
          return TouchResponseType::HOLD_SUPPRESS;
      }
  }
}

// When a contest ends, any held interactions will have their responses updated.
//
// This simply translates from consumption status to response type.
TouchResponseType updatedResponse(InteractionTracker::ConsumptionStatus status) {
  switch (status) {
    case InteractionTracker::ConsumptionStatus::kUndecided:
      FX_DCHECK(false) << "held interactions should only be updated when the contest is resolved";
      return TouchResponseType::NO;
    case InteractionTracker::ConsumptionStatus::kAccept:
      return TouchResponseType::YES_PRIORITIZE;
    case InteractionTracker::ConsumptionStatus::kReject:
      return TouchResponseType::NO;
  }
}

}  // namespace

GestureManagerV2::GestureManagerV2(TouchSourceWithLocalHitPtr touch_source)
    : GestureManagerV2(std::move(touch_source),
                       [](InteractionTracker::HeldInteractionCallback callback) {
                         return std::make_unique<GestureArenaV2>(std::move(callback));
                       }) {}

GestureManagerV2::GestureManagerV2(TouchSourceWithLocalHitPtr touch_source,
                                   ArenaFactory arena_factory)
    : touch_source_(std::move(touch_source)) {
  // Park a callback that will notify the TouchSource (via UpdateResponse) when
  // a held interaction becomes decided.
  auto callback = [this](fuchsia::ui::pointer::TouchInteractionId interaction,
                         uint64_t trace_flow_id, InteractionTracker::ConsumptionStatus status) {
    FX_DCHECK(status != InteractionTracker::ConsumptionStatus::kUndecided);

    TouchResponse response;
    response.set_response_type(updatedResponse(status));
    response.set_trace_flow_id(trace_flow_id);
    touch_source_->UpdateResponse(interaction, std::move(response), [](auto...) {});
  };

  arena_ = arena_factory(std::move(callback));
  FX_DCHECK(arena_);

  WatchForTouchEvents({});
}

void GestureManagerV2::WatchForTouchEvents(std::vector<TouchResponse> old_responses) {
  auto callback = [this](std::vector<TouchEventWithLocalHit> events) {
    auto responses = HandleEvents(std::move(events));
    WatchForTouchEvents(std::move(responses));
  };
  touch_source_->Watch(std::move(old_responses), callback);
}

std::vector<fuchsia::ui::pointer::TouchResponse> GestureManagerV2::HandleEvents(
    std::vector<fuchsia::ui::pointer::augment::TouchEventWithLocalHit> events) {
  std::vector<TouchResponse> responses;

  for (uint32_t i = 0; i < events.size(); ++i) {
    const auto& event = events[i];

    if (event.touch_event.has_device_info()) {
      FX_DCHECK(event.touch_event.device_info().has_id());
      FX_DCHECK(!touch_device_id_.has_value());
      touch_device_id_ = event.touch_event.device_info().id();
    }

    if (event.touch_event.has_view_parameters()) {
      viewport_bounds_ = event.touch_event.view_parameters().viewport;
    }

    auto response = HandleEvent(events[i]);
    responses.push_back(std::move(response));
  }

  return responses;
}

fuchsia::ui::pointer::TouchResponse GestureManagerV2::HandleEvent(
    const fuchsia::ui::pointer::augment::TouchEventWithLocalHit& event) {
  if (!event.touch_event.has_pointer_sample()) {
    // For non-sample events, the TouchSource API expects an empty response.
    return {};
  }

  fuchsia::ui::pointer::TouchResponse response;

  FX_DCHECK(event.touch_event.has_trace_flow_id());
  response.set_trace_flow_id(event.touch_event.trace_flow_id());

  FX_DCHECK(event.touch_event.pointer_sample().has_phase());
  const auto contest_status = arena_->OnEvent(event);
  const auto phase = event.touch_event.pointer_sample().phase();
  const auto response_type = initialResponse(contest_status, phase);
  response.set_response_type(response_type);

  return response;
}

}  // namespace a11y
